#include "NetworkServer.h"
#include "CertificateManager.h"
#include "HtmlAssets.h"
#include <civetweb.h>
#include <algorithm>
#include <cmath>
#include <cstring>

#if JUCE_WINDOWS
 #include <winsock2.h>
 #include <iphlpapi.h>
#endif

namespace pgstream
{
namespace
{
juce::String jsonString(const juce::String& value)
{
    auto escaped = value.replace("\\", "\\\\")
                        .replace("\"", "\\\"")
                        .replace("\r", "\\r")
                        .replace("\n", "\\n");
    return "\"" + escaped + "\"";
}

juce::String jsonStringArray(const juce::StringArray& values)
{
    juce::String json("[");
    for (int i = 0; i < values.size(); ++i)
    {
        if (i > 0)
            json += ",";

        json += jsonString(values[i]);
    }

    json += "]";
    return json;
}
}

NetworkServer::NetworkServer(AudioTapFifo& fifoToRead)
    : juce::Thread("PGStream network worker"), fifo(fifoToRead)
{
    readBuffer.resize(8192);
    packetBuffer.resize(8192);
    silenceBuffer.resize(4096);
}

NetworkServer::~NetworkServer()
{
    stopServer();
}

void NetworkServer::setSessionSampleRate(double sampleRate)
{
    if (sampleRate > 1.0)
        sessionSampleRate.store(sampleRate, std::memory_order_release);
}

void NetworkServer::applyConfig(const StreamConfig& newConfig)
{
    auto adjustedConfig = newConfig;
    adjustedConfig.sessionSampleRate = sessionSampleRate.load(std::memory_order_acquire);

    bool needsRestart = false;
    {
        std::lock_guard<std::mutex> lock(configMutex);
        needsRestart = contextRunning.load(std::memory_order_acquire)
            && adjustedConfig.streamEnabled
            && adjustedConfig.port != config.port;
        config = adjustedConfig;
    }

    if (!adjustedConfig.streamEnabled)
    {
        enabled.store(false, std::memory_order_release);
        stopServer();
        updateStatus("disabled");
        return;
    }

    if (contextRunning.load(std::memory_order_acquire) && ! needsRestart)
    {
        enabled.store(true, std::memory_order_release);
        return;
    }

    stopServer();
    if (startServer(adjustedConfig))
    {
        enabled.store(true, std::memory_order_release);
        resampler.reset();
        startThread();
    }
    else
    {
        enabled.store(false, std::memory_order_release);
    }
}

bool NetworkServer::startServer(const StreamConfig& newConfig)
{
    struct mg_callbacks callbacks;
    std::memset(&callbacks, 0, sizeof(callbacks));
    callbacks.init_ssl = &NetworkServer::initSslHandler;
    callbacks.log_message = &NetworkServer::logHandler;
    callbacks.log_access = &NetworkServer::logHandler;

    const auto portString = juce::String(newConfig.port) + "s";
    const char* options[] = {
        "listening_ports", portString.toRawUTF8(),
        "num_threads", "4",
        "enable_websocket_ping_pong", "yes",
        "websocket_timeout_ms", "30000",
        "request_timeout_ms", "1500",
        "linger_timeout_ms", "0",
        "authentication_domain", "PGStream",
        "ssl_protocol_version", "4",
        nullptr
    };

    context = mg_start(&callbacks, this, options);
    if (context == nullptr)
    {
        updateStatus("port bind or TLS start failed");
        contextRunning.store(false, std::memory_order_release);
        return false;
    }

    mg_set_request_handler(context, "/", &NetworkServer::requestHandler, this);
    mg_set_request_handler(context, "/index.html", &NetworkServer::requestHandler, this);
    mg_set_request_handler(context, "/app.js", &NetworkServer::requestHandler, this);
    mg_set_request_handler(context, "/audio-worklet.js", &NetworkServer::requestHandler, this);
    mg_set_request_handler(context, "/style.css", &NetworkServer::requestHandler, this);
    mg_set_request_handler(context, "/pgs.png", &NetworkServer::requestHandler, this);
    mg_set_request_handler(context, "/healthz", &NetworkServer::requestHandler, this);
    mg_set_request_handler(context, "/info", &NetworkServer::requestHandler, this);
    mg_set_request_handler(context, "/cert-info", &NetworkServer::requestHandler, this);
    mg_set_websocket_handler(context,
                             "/ws",
                             &NetworkServer::wsConnectHandler,
                             &NetworkServer::wsReadyHandler,
                             &NetworkServer::wsDataHandler,
                             &NetworkServer::wsCloseHandler,
                             this);

    refreshLanSelection(newConfig.port);
    updateStatus("streaming");

    contextRunning.store(true, std::memory_order_release);
    return true;
}

void NetworkServer::stopServer()
{
    enabled.store(false, std::memory_order_release);
    signalThreadShouldExit();
    if (isThreadRunning())
        stopThread(3000);

    if (context != nullptr)
    {
        mg_stop(context);
        context = nullptr;
    }

    hub.clear();
    contextRunning.store(false, std::memory_order_release);

    std::lock_guard<std::mutex> lock(statusMutex);
    lanUrl.clear();
    currentLanIp.clear();
    candidateLanUrls.clear();
    candidateLanDescriptions.clear();
}

void NetworkServer::run()
{
    auto previousSourceRate = 0;
    auto previousTargetRate = 0;
    auto previousPacketFrames = size_t { 0 };
    auto previousOutputFormat = OutputFormat::float32;
    auto lastSourceAudioMs = juce::Time::getMillisecondCounterHiRes();
    auto nextSilencePacketMs = lastSourceAudioMs;
    auto nextLanRefreshMs = lastSourceAudioMs;
    auto sourceHasBeenActive = false;
    auto starvationReported = false;

    resetPacketAccumulator();

    while (!threadShouldExit())
    {
        StreamConfig localConfig;
        {
            std::lock_guard<std::mutex> lock(configMutex);
            localConfig = config;
        }

        localConfig.sessionSampleRate = sessionSampleRate.load(std::memory_order_acquire);
        const auto sourceRate = juce::jmax(1, static_cast<int> (std::round(localConfig.sessionSampleRate)));
        const auto targetRate = localConfig.targetSampleRate();
        const auto packetFrames = static_cast<size_t> (localConfig.packetFrameCount());
        const auto packetDurationMs = localConfig.packetDurationMs();
        const auto clients = hub.count();
        const auto nowMs = juce::Time::getMillisecondCounterHiRes();

        if (nowMs >= nextLanRefreshMs)
        {
            refreshLanSelection(localConfig.port);
            nextLanRefreshMs = nowMs + 5000.0;
        }

        if (sourceRate != previousSourceRate
            || targetRate != previousTargetRate
            || packetFrames != previousPacketFrames
            || localConfig.outputFormat != previousOutputFormat)
        {
            resetPacketAccumulator();
            resampler.reset();
            previousSourceRate = sourceRate;
            previousTargetRate = targetRate;
            previousPacketFrames = packetFrames;
            previousOutputFormat = localConfig.outputFormat;
            lastSourceAudioMs = nowMs;
            nextSilencePacketMs = nowMs + packetDurationMs;
            starvationReported = false;
        }

        const auto desiredReadFrames = juce::jmax<size_t> (4096, packetFrames);
        if (readBuffer.size() < desiredReadFrames * 2)
            readBuffer.resize(desiredReadFrames * 2);

        if (packetBuffer.size() < packetFrames * 2)
            packetBuffer.resize(packetFrames * 2);

        if (silenceBuffer.size() < packetFrames * 2)
            silenceBuffer.resize(packetFrames * 2, 0.0f);

        if (clients == 0)
        {
            resetPacketAccumulator();
            fifo.pop(readBuffer.data(), readBuffer.size() / 2);
            wait(20);
            continue;
        }

        const auto framesRead = fifo.pop(readBuffer.data(), readBuffer.size() / 2);
        if (framesRead > 0)
        {
            lastSourceAudioMs = nowMs;
            nextSilencePacketMs = nowMs + packetDurationMs;
            sourceHasBeenActive = true;
            starvationReported = false;

            const float* framesToSend = readBuffer.data();
            auto sendFrameCount = framesRead;

            if (sourceRate != targetRate)
            {
                resampler.process(readBuffer.data(), framesRead, sourceRate, targetRate, resampledBuffer);
                framesToSend = resampledBuffer.data();
                sendFrameCount = resampledBuffer.size() / 2;
            }

            if (sendFrameCount > 0)
                appendAndBroadcastPackets(framesToSend, sendFrameCount, localConfig, targetRate, packetFrames);

            wait(packetBufferFrames == 0 ? 1 : juce::jlimit(2, 10, packetDurationMs / 4));
            continue;
        }

        const auto idleTimeoutMs = juce::jmax(250.0, static_cast<double> (packetDurationMs * 4));
        if (nowMs - lastSourceAudioMs >= idleTimeoutMs)
        {
            resetPacketAccumulator();

            if (sourceHasBeenActive && ! starvationReported)
            {
                serverFifoUnderruns.fetch_add(1, std::memory_order_relaxed);
                starvationReported = true;
            }

            if (!localConfig.keepAliveWhenIdle || clients == 0)
            {
                wait(10);
                continue;
            }

            if (nowMs >= nextSilencePacketMs)
            {
                std::fill(silenceBuffer.begin(),
                          silenceBuffer.begin() + static_cast<std::ptrdiff_t> (packetFrames * 2),
                          0.0f);
                buildAndBroadcastFrame(silenceBuffer.data(), packetFrames, localConfig, targetRate, true);
                nextSilencePacketMs = nowMs + packetDurationMs;
            }

            wait(juce::jlimit(2, 20, packetDurationMs / 2));
            continue;
        }

        wait(juce::jlimit(2, 10, packetDurationMs / 4));
    }

    resetPacketAccumulator();
}

void NetworkServer::appendAndBroadcastPackets(const float* interleavedStereo,
                                              size_t frameCount,
                                              const StreamConfig& frameConfig,
                                              int sampleRate,
                                              size_t packetFrames)
{
    if (interleavedStereo == nullptr || frameCount == 0 || packetFrames == 0)
        return;

    if (packetBuffer.size() < packetFrames * 2)
        packetBuffer.resize(packetFrames * 2);

    size_t sourceFrame = 0;
    while (sourceFrame < frameCount)
    {
        const auto availablePacketFrames = packetFrames - packetBufferFrames;
        const auto framesToCopy = std::min(availablePacketFrames, frameCount - sourceFrame);

        std::copy(interleavedStereo + sourceFrame * 2,
                  interleavedStereo + (sourceFrame + framesToCopy) * 2,
                  packetBuffer.data() + packetBufferFrames * 2);

        packetBufferFrames += framesToCopy;
        sourceFrame += framesToCopy;

        if (packetBufferFrames == packetFrames)
        {
            buildAndBroadcastFrame(packetBuffer.data(), packetFrames, frameConfig, sampleRate, false);
            packetBufferFrames = 0;
        }
    }
}

void NetworkServer::resetPacketAccumulator()
{
    packetBufferFrames = 0;
}

void NetworkServer::buildAndBroadcastFrame(const float* interleavedStereo,
                                           size_t frameCount,
                                           const StreamConfig& frameConfig,
                                           int sampleRate,
                                           bool silence)
{
    if (interleavedStereo == nullptr || frameCount == 0)
        return;

    const auto payloadBytes = frameConfig.outputFormat == OutputFormat::float32 ? frameCount * 2 * sizeof(float)
                                                                                : frameCount * 2 * sizeof(int16_t);
    frameBuffer.clear();
    frameBuffer.reserve(frameHeaderBytes + payloadBytes);

    frameBuffer.push_back('P');
    frameBuffer.push_back('G');
    frameBuffer.push_back('S');
    frameBuffer.push_back('1');
    appendU32(protocolVersion);
    appendU32(sequence.fetch_add(1, std::memory_order_relaxed));
    appendU32(static_cast<uint32_t> (sampleRate));
    appendU16(2);
    appendU16(static_cast<uint16_t> (frameConfig.outputFormat));
    appendU32(static_cast<uint32_t> (frameCount));
    appendU32(silence ? frameFlagSilence : 0u);

    if (frameConfig.outputFormat == OutputFormat::float32)
    {
        const auto* bytes = reinterpret_cast<const uint8_t*> (interleavedStereo);
        frameBuffer.insert(frameBuffer.end(), bytes, bytes + payloadBytes);
    }
    else
    {
        for (size_t i = 0; i < frameCount * 2; ++i)
        {
            const auto clamped = juce::jlimit(-1.0f, 1.0f, interleavedStereo[i]);
            const auto pcm = static_cast<int16_t> (std::lrint(clamped * 32767.0f));
            frameBuffer.push_back(static_cast<uint8_t> (pcm & 0xff));
            frameBuffer.push_back(static_cast<uint8_t> ((pcm >> 8) & 0xff));
        }
    }

    const auto broadcast = hub.broadcastBinary(frameBuffer.data(), frameBuffer.size());
    if (broadcast.successfulSends > 0)
        networkPacketsSent.fetch_add(static_cast<uint64_t> (broadcast.successfulSends), std::memory_order_relaxed);

    if (broadcast.sendFailures > 0)
        websocketSendFailures.fetch_add(static_cast<uint64_t> (broadcast.sendFailures), std::memory_order_relaxed);

    framesSent.fetch_add(frameCount, std::memory_order_relaxed);
}

void NetworkServer::appendU16(uint16_t value)
{
    frameBuffer.push_back(static_cast<uint8_t> (value & 0xff));
    frameBuffer.push_back(static_cast<uint8_t> ((value >> 8) & 0xff));
}

void NetworkServer::appendU32(uint32_t value)
{
    frameBuffer.push_back(static_cast<uint8_t> (value & 0xff));
    frameBuffer.push_back(static_cast<uint8_t> ((value >> 8) & 0xff));
    frameBuffer.push_back(static_cast<uint8_t> ((value >> 16) & 0xff));
    frameBuffer.push_back(static_cast<uint8_t> ((value >> 24) & 0xff));
}

void NetworkServer::refreshLanSelection(int port)
{
    const auto networkSelection = selectBestLanInterface(port);

    std::lock_guard<std::mutex> lock(statusMutex);
    lanUrl = networkSelection.primaryUrl;
    currentLanIp = networkSelection.primaryAddress;
    candidateLanUrls = networkSelection.candidateUrls;
    candidateLanDescriptions = networkSelection.candidateDescriptions;
    listenAddress = "0.0.0.0";
}

int NetworkServer::handleHttpRequest(mg_connection* connection)
{
    const auto* info = mg_get_request_info(connection);
    const auto* uri = info != nullptr ? info->local_uri : "/";

    if (std::strcmp(uri, "/healthz") == 0)
    {
        const char* json = "{\"ok\":true,\"name\":\"PGStream\"}";
        mg_send_http_ok(connection, "application/json; charset=utf-8", std::strlen(json));
        mg_write(connection, json, std::strlen(json));
        return 200;
    }

    if (std::strcmp(uri, "/info") == 0)
    {
        StreamConfig localConfig;
        {
            std::lock_guard<std::mutex> lock(configMutex);
            localConfig = config;
        }
        localConfig.sessionSampleRate = sessionSampleRate.load(std::memory_order_acquire);
        const auto stats = getStats();

        const auto json = juce::String("{\"name\":\"PGStream\",\"format\":\"")
            + localConfig.formatName()
            + "\",\"formatCode\":"
            + juce::String(static_cast<int> (localConfig.outputFormat))
            + ",\"sampleRate\":"
            + juce::String(localConfig.targetSampleRate())
            + ",\"channels\":2,\"bufferTargetMs\":"
            + juce::String(localConfig.bufferTargetMs)
            + ",\"packetDurationMs\":"
            + juce::String(localConfig.packetDurationMs())
            + ",\"packetMode\":"
            + jsonString(localConfig.packetModeName())
            + ",\"server\":{\"serverFifoUnderruns\":"
            + juce::String(stats.serverFifoUnderruns)
            + ",\"networkPacketsSent\":"
            + juce::String(stats.networkPacketsSent)
            + ",\"websocketSendFailures\":"
            + juce::String(stats.websocketSendFailures)
            + ",\"connectedClients\":"
            + juce::String(stats.connectedClients)
            + ",\"currentLanIp\":"
            + jsonString(stats.currentLanIp)
            + ",\"listenAddress\":"
            + jsonString(stats.listenAddress)
            + ",\"port\":"
            + juce::String(stats.port)
            + ",\"streamFormat\":"
            + jsonString(stats.streamFormat)
            + ",\"streamSampleRate\":"
            + juce::String(stats.streamSampleRate)
            + ",\"framesSent\":"
            + juce::String(stats.framesSent)
            + ",\"status\":"
            + jsonString(stats.statusText)
            + ",\"lanUrl\":"
            + jsonString(stats.lanUrl)
            + ",\"candidateLanUrls\":"
            + jsonStringArray(stats.candidateLanUrls)
            + "}}";

        mg_send_http_ok(connection, "application/json; charset=utf-8", json.getNumBytesAsUTF8());
        mg_write(connection, json.toRawUTF8(), json.getNumBytesAsUTF8());
        return 200;
    }

    if (std::strcmp(uri, "/cert-info") == 0)
    {
        const auto json = juce::String("{\"certificate\":\"")
            + CertificateManager::certificateSummary()
            + "\"}";
        mg_send_http_ok(connection, "application/json; charset=utf-8", json.getNumBytesAsUTF8());
        mg_write(connection, json.toRawUTF8(), json.getNumBytesAsUTF8());
        return 200;
    }

    HtmlAsset asset;
    if (getHtmlAsset(uri, asset))
    {
        mg_send_http_ok(connection, asset.mimeType, asset.size);
        mg_write(connection, asset.data, asset.size);
        return 200;
    }

    mg_send_http_error(connection, 404, "Not found");
    return 404;
}

int NetworkServer::handleInitSsl(void* sslContext)
{
    return CertificateManager::installIntoSslContext(sslContext);
}

int NetworkServer::requestHandler(mg_connection* connection, void* cbdata)
{
    return static_cast<NetworkServer*> (cbdata)->handleHttpRequest(connection);
}

int NetworkServer::wsConnectHandler(const mg_connection*, void*)
{
    return 0;
}

void NetworkServer::wsReadyHandler(mg_connection* connection, void* cbdata)
{
    static_cast<NetworkServer*> (cbdata)->hub.add(connection);
}

int NetworkServer::wsDataHandler(mg_connection*, int, char*, size_t, void*)
{
    return 1;
}

void NetworkServer::wsCloseHandler(const mg_connection* connection, void* cbdata)
{
    static_cast<NetworkServer*> (cbdata)->hub.remove(const_cast<mg_connection*> (connection));
}

int NetworkServer::initSslHandler(void* sslContext, void* userData)
{
    return static_cast<NetworkServer*> (userData)->handleInitSsl(sslContext);
}

int NetworkServer::logHandler(const mg_connection*, const char*)
{
    return 1;
}

void NetworkServer::updateStatus(juce::String text)
{
    std::lock_guard<std::mutex> lock(statusMutex);
    statusText = std::move(text);
}

StreamStats NetworkServer::getStats() const
{
    StreamConfig localConfig;
    {
        std::lock_guard<std::mutex> lock(configMutex);
        localConfig = config;
    }

    StreamStats stats;
    stats.serverRunning = contextRunning.load(std::memory_order_acquire);
    stats.streamEnabled = enabled.load(std::memory_order_acquire);
    stats.port = localConfig.port;
    stats.connectedClients = hub.count();
    stats.streamFormat = localConfig.formatName();
    stats.streamSampleRate = localConfig.targetSampleRate();
    stats.bufferTargetMs = localConfig.bufferTargetMs;
    stats.packetDurationMs = localConfig.packetDurationMs();
    stats.packetMode = localConfig.packetModeName();
    stats.framesSent = framesSent.load(std::memory_order_relaxed);
    stats.serverFifoUnderruns = serverFifoUnderruns.load(std::memory_order_relaxed);
    stats.networkPacketsSent = networkPacketsSent.load(std::memory_order_relaxed);
    stats.websocketSendFailures = websocketSendFailures.load(std::memory_order_relaxed);
    stats.fifoDroppedFrames = fifo.getDroppedFrames();

    {
        std::lock_guard<std::mutex> lock(statusMutex);
        stats.lanUrl = lanUrl;
        stats.currentLanIp = currentLanIp;
        stats.listenAddress = listenAddress;
        stats.statusText = statusText;
        stats.candidateLanUrls = candidateLanUrls;
        stats.candidateLanDescriptions = candidateLanDescriptions;
    }

    return stats;
}
}
