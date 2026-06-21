#include "NetworkServer.h"
#include "HtmlAssets.h"
#include <civetweb.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

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

juce::String jsonPropertyString(const juce::var& object, const char* name)
{
    if (auto* dynamic = object.getDynamicObject())
        return dynamic->getProperty(name).toString();

    return {};
}

int jsonPropertyInt(const juce::var& object, const char* name, int fallback)
{
    if (auto* dynamic = object.getDynamicObject())
    {
        const auto value = dynamic->getProperty(name);
        if (value.isInt() || value.isInt64() || value.isDouble() || value.isBool())
            return static_cast<int> (value);

        const auto text = value.toString();
        if (text.isNotEmpty())
            return text.getIntValue();
    }

    return fallback;
}

OpusBitrateMode parseBrowserBitrateMode(const juce::var& message, OpusBitrateMode fallback)
{
    const auto preset = jsonPropertyString(message, "bitratePreset").toLowerCase();
    const auto bps = jsonPropertyInt(message, "bitrateBps", 0);

    if (preset.contains("128") || bps == 128000)
        return OpusBitrateMode::goodPreview128;
    if (preset.contains("192") || bps == 192000)
        return OpusBitrateMode::veryGood192;
    if (preset.contains("256") || bps == 256000)
        return OpusBitrateMode::studioPreview256;
    if (preset.contains("510") || bps == 510000)
        return OpusBitrateMode::experimentalMax510;
    if (preset.contains("320") || bps == 320000)
        return OpusBitrateMode::highQuality320;

    return fallback;
}

LatencyMode parseBrowserLatencyMode(const juce::var& message, LatencyMode fallback)
{
    const auto preset = jsonPropertyString(message, "latencyPreset").toLowerCase();

    if (preset.contains("safe"))
        return LatencyMode::safe;
    if (preset.contains("ultra"))
        return LatencyMode::ultraLowExperimental;
    if (preset.contains("low"))
        return LatencyMode::lowLatency;
    if (preset.contains("balanced"))
        return LatencyMode::balanced;

    return fallback;
}
}

NetworkServer::NetworkServer(AudioTapFifo& fifoToRead)
    : juce::Thread("PGStream network worker"), fifo(fifoToRead)
{
    readBuffer.resize(8192);
    webrtcSilenceBuffer.resize(1920);
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

void NetworkServer::setActiveProfileCallback(std::function<void(OpusBitrateMode, LatencyMode)> callback)
{
    std::lock_guard<std::mutex> lock(callbackMutex);
    activeProfileCallback = std::move(callback);
}

void NetworkServer::applyConfig(const StreamConfig& newConfig)
{
    auto adjustedConfig = newConfig;
    adjustedConfig.sessionSampleRate = sessionSampleRate.load(std::memory_order_acquire);
    webrtcSender.applyConfig(adjustedConfig);

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
        webrtcSender.clear();
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
        webrtcResampler.reset();
        webrtcSender.resetAudio();
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
    callbacks.log_message = &NetworkServer::logHandler;
    callbacks.log_access = &NetworkServer::logHandler;

    const auto portString = juce::String(newConfig.port);
    const char* options[] = {
        "listening_ports", portString.toRawUTF8(),
        "num_threads", "4",
        "enable_websocket_ping_pong", "yes",
        "websocket_timeout_ms", "30000",
        "request_timeout_ms", "1500",
        "linger_timeout_ms", "0",
        "authentication_domain", "PGStream",
        nullptr
    };

    context = mg_start(&callbacks, this, options);
    if (context == nullptr)
    {
        updateStatus("port bind or server start failed");
        contextRunning.store(false, std::memory_order_release);
        return false;
    }

    mg_set_request_handler(context, "/", &NetworkServer::requestHandler, this);
    mg_set_request_handler(context, "/index.html", &NetworkServer::requestHandler, this);
    mg_set_request_handler(context, "/app.js", &NetworkServer::requestHandler, this);
    mg_set_request_handler(context, "/style.css", &NetworkServer::requestHandler, this);
    mg_set_request_handler(context, "/pgs.png", &NetworkServer::requestHandler, this);
    mg_set_request_handler(context, "/healthz", &NetworkServer::requestHandler, this);
    mg_set_request_handler(context, "/info", &NetworkServer::requestHandler, this);
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

    webrtcSender.clear();
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
    auto previousWebRtcFrameFrames = size_t { 0 };
    auto lastSourceAudioMs = juce::Time::getMillisecondCounterHiRes();
    auto nextSilencePacketMs = lastSourceAudioMs;
    auto nextLanRefreshMs = lastSourceAudioMs;
    auto sourceHasBeenActive = false;
    auto starvationReported = false;

    while (!threadShouldExit())
    {
        StreamConfig localConfig;
        {
            std::lock_guard<std::mutex> lock(configMutex);
            localConfig = config;
        }

        localConfig.sessionSampleRate = sessionSampleRate.load(std::memory_order_acquire);
        const auto sourceRate = juce::jmax(1, static_cast<int> (std::round(localConfig.sessionSampleRate)));
        const auto webRtcFrameFrames = juce::jmax<size_t> (1, webrtcSender.opusFrameCount());
        const auto webRtcFrameDurationMs = juce::jmax(1, static_cast<int> (std::round(static_cast<double> (webRtcFrameFrames) * 1000.0 / 48000.0)));
        const auto webRtcTracks = webrtcSender.openTrackCount();
        const auto nowMs = juce::Time::getMillisecondCounterHiRes();

        if (nowMs >= nextLanRefreshMs)
        {
            refreshLanSelection(localConfig.port);
            nextLanRefreshMs = nowMs + 5000.0;
        }

        if (sourceRate != previousSourceRate || webRtcFrameFrames != previousWebRtcFrameFrames)
        {
            webrtcResampler.reset();
            webrtcSender.resetAudio();
            previousSourceRate = sourceRate;
            previousWebRtcFrameFrames = webRtcFrameFrames;
            lastSourceAudioMs = nowMs;
            nextSilencePacketMs = nowMs + webRtcFrameDurationMs;
            sourceHasBeenActive = false;
            starvationReported = false;
        }

        auto desiredReadFrames = sourceRate == 48000
            ? webRtcFrameFrames
            : static_cast<size_t> (std::ceil(static_cast<double> (webRtcFrameFrames) * static_cast<double> (sourceRate) / 48000.0)) + 4;
        desiredReadFrames = juce::jlimit<size_t> (1, 4096, desiredReadFrames);
        if (readBuffer.size() < desiredReadFrames * 2)
            readBuffer.resize(desiredReadFrames * 2);

        if (webrtcSilenceBuffer.size() < webRtcFrameFrames * 2)
            webrtcSilenceBuffer.resize(webRtcFrameFrames * 2, 0.0f);

        if (webRtcTracks == 0)
        {
            webrtcSender.resetAudio();
            fifo.pop(readBuffer.data(), readBuffer.size() / 2);
            sourceHasBeenActive = false;
            starvationReported = false;
            lastSourceAudioMs = nowMs;
            wait(20);
            continue;
        }

        const auto framesRead = fifo.pop(readBuffer.data(), desiredReadFrames);
        if (framesRead > 0)
        {
            lastSourceAudioMs = nowMs;
            nextSilencePacketMs = nowMs + webRtcFrameDurationMs;
            sourceHasBeenActive = true;
            starvationReported = false;

            const float* webRtcFrames = readBuffer.data();
            auto webRtcFrameCount = framesRead;

            if (sourceRate != 48000)
            {
                webrtcResampler.process(readBuffer.data(), framesRead, sourceRate, 48000, webrtcResampledBuffer);
                webRtcFrames = webrtcResampledBuffer.data();
                webRtcFrameCount = webrtcResampledBuffer.size() / 2;
            }

            if (webRtcFrameCount > 0)
            {
                webrtcSender.encodeAndSend(webRtcFrames, webRtcFrameCount);
                framesSent.fetch_add(static_cast<uint64_t> (webRtcFrameCount), std::memory_order_relaxed);
            }

            wait(juce::jlimit(1, 10, webRtcFrameDurationMs / 4));
            continue;
        }

        const auto idleTimeoutMs = juce::jmax(250.0, static_cast<double> (webRtcFrameDurationMs * 4));
        if (nowMs - lastSourceAudioMs >= idleTimeoutMs)
        {
            if (sourceHasBeenActive && ! starvationReported)
            {
                serverFifoUnderruns.fetch_add(1, std::memory_order_relaxed);
                starvationReported = true;
            }

            if (!localConfig.keepAliveWhenIdle)
            {
                wait(10);
                continue;
            }

            if (nowMs >= nextSilencePacketMs)
            {
                std::fill(webrtcSilenceBuffer.begin(),
                          webrtcSilenceBuffer.begin() + static_cast<std::ptrdiff_t> (webRtcFrameFrames * 2),
                          0.0f);
                webrtcSender.encodeAndSend(webrtcSilenceBuffer.data(), webRtcFrameFrames);
                framesSent.fetch_add(static_cast<uint64_t> (webRtcFrameFrames), std::memory_order_relaxed);
                nextSilencePacketMs = nowMs + webRtcFrameDurationMs;
            }

            wait(juce::jlimit(1, 20, webRtcFrameDurationMs / 2));
            continue;
        }

        wait(juce::jlimit(1, 10, webRtcFrameDurationMs / 4));
    }
}

void NetworkServer::handleWebSocketText(mg_connection* connection, const char* data, size_t dataLen)
{
    if (connection == nullptr || data == nullptr || dataLen == 0)
        return;

    const auto text = juce::String::fromUTF8(data, static_cast<int> (dataLen));
    const auto message = juce::JSON::parse(text);
    const auto type = jsonPropertyString(message, "type");

    if (type == "webrtc-offer")
    {
        auto changed = false;
        OpusBitrateMode activeBitrateMode;
        LatencyMode activeLatencyMode;
        {
            std::lock_guard<std::mutex> lock(configMutex);
            activeBitrateMode = parseBrowserBitrateMode(message, config.opusBitrateMode);
            activeLatencyMode = parseBrowserLatencyMode(message, config.latencyMode);
            changed = activeBitrateMode != config.opusBitrateMode
                || activeLatencyMode != config.latencyMode;

            if (changed)
            {
                config.opusBitrateMode = activeBitrateMode;
                config.latencyMode = activeLatencyMode;
            }
        }

        if (changed)
            notifyActiveProfileChanged(activeBitrateMode, activeLatencyMode);

        webrtcSender.handleOffer(connection, message);
        return;
    }

    if (type == "webrtc-candidate")
    {
        webrtcSender.handleRemoteCandidate(connection, message);
        return;
    }

    if (type == "webrtc-stop")
        webrtcSender.removeConnection(connection);
}

void NetworkServer::notifyActiveProfileChanged(OpusBitrateMode bitrateMode, LatencyMode latencyMode)
{
    std::function<void(OpusBitrateMode, LatencyMode)> callback;
    {
        std::lock_guard<std::mutex> lock(callbackMutex);
        callback = activeProfileCallback;
    }

    if (callback)
        callback(bitrateMode, latencyMode);
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
        const auto stats = getStats();

        juce::String json;
        json.preallocateBytes(1536);
        json += "{\"name\":\"PGStream\",\"transport\":";
        json += jsonString(stats.transportMode);
        json += ",\"opusCodec\":\"opus/48000/2\",\"opusBitrateBps\":";
        json += juce::String(stats.opusBitrateBps);
        json += ",\"opusBitratePreset\":";
        json += jsonString(stats.opusBitratePreset);
        json += ",\"opusBitrateLimited\":";
        json += stats.opusBitrateLimited ? "true" : "false";
        json += ",\"latencyPreset\":";
        json += jsonString(stats.latencyMode);
        json += ",\"latencyTarget\":";
        json += jsonString(stats.latencyTarget);
        json += ",\"opusFrameDurationMs\":";
        json += juce::String(stats.opusFrameDurationMs);
        json += ",\"inputSampleRate\":";
        json += juce::String(stats.inputSampleRate);
        json += ",\"server\":{\"serverFifoUnderruns\":";
        json += juce::String(stats.serverFifoUnderruns);
        json += ",\"webrtcPeerCount\":";
        json += juce::String(stats.webrtcPeerCount);
        json += ",\"webrtcOpenTracks\":";
        json += juce::String(stats.webrtcOpenTracks);
        json += ",\"webrtcConnectionState\":";
        json += jsonString(stats.webrtcConnectionState);
        json += ",\"webrtcIceConnectionState\":";
        json += jsonString(stats.webrtcIceConnectionState);
        json += ",\"currentLanIp\":";
        json += jsonString(stats.currentLanIp);
        json += ",\"listenAddress\":";
        json += jsonString(stats.listenAddress);
        json += ",\"port\":";
        json += juce::String(stats.port);
        json += ",\"inputSampleRate\":";
        json += juce::String(stats.inputSampleRate);
        json += ",\"framesSent\":";
        json += juce::String(stats.framesSent);
        json += ",\"senderQueueFillFrames\":";
        json += juce::String(static_cast<int64_t> (stats.senderQueueFillFrames));
        json += ",\"senderQueueCapacityFrames\":";
        json += juce::String(static_cast<int64_t> (stats.senderQueueCapacityFrames));
        json += ",\"senderQueueFillMs\":";
        json += juce::String(stats.senderQueueFillMs, 2);
        json += ",\"senderDroppedFrames\":";
        json += juce::String(stats.fifoDroppedFrames);
        json += ",\"webrtcEncodedFrames\":";
        json += juce::String(stats.webrtcEncodedFrames);
        json += ",\"webrtcEncodedPackets\":";
        json += juce::String(stats.webrtcEncodedPackets);
        json += ",\"webrtcEncodedBytes\":";
        json += juce::String(stats.webrtcEncodedBytes);
        json += ",\"webrtcSendCalls\":";
        json += juce::String(stats.webrtcSendCalls);
        json += ",\"webrtcRtpPacketsAttempted\":";
        json += juce::String(stats.webrtcRtpPacketsAttempted);
        json += ",\"webrtcRtpPacketsSent\":";
        json += juce::String(stats.webrtcRtpPacketsSent);
        json += ",\"webrtcRtpSendFailures\":";
        json += juce::String(stats.webrtcRtpSendFailures);
        json += ",\"webrtcEncoderOverloadWarnings\":";
        json += juce::String(stats.webrtcEncoderOverloadWarnings);
        json += ",\"status\":";
        json += jsonString(stats.statusText);
        json += ",\"lanUrl\":";
        json += jsonString(stats.lanUrl);
        json += ",\"candidateLanUrls\":";
        json += jsonStringArray(stats.candidateLanUrls);
        json += "}}";

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
    juce::ignoreUnused(connection, cbdata);
}

int NetworkServer::wsDataHandler(mg_connection* connection, int bits, char* data, size_t dataLen, void* cbdata)
{
    const auto opcode = bits & 0x0f;
    if (opcode == MG_WEBSOCKET_OPCODE_TEXT)
        static_cast<NetworkServer*> (cbdata)->handleWebSocketText(connection, data, dataLen);

    return 1;
}

void NetworkServer::wsCloseHandler(const mg_connection* connection, void* cbdata)
{
    auto* server = static_cast<NetworkServer*> (cbdata);
    auto* mutableConnection = const_cast<mg_connection*> (connection);
    server->webrtcSender.removeConnection(mutableConnection);
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
    stats.connectedClients = webrtcSender.peerCount();
    stats.transportMode = localConfig.transportModeName();
    stats.inputSampleRate = juce::jmax(1, static_cast<int> (std::round(localConfig.sessionSampleRate)));
    stats.opusBitrateBps = localConfig.opusBitrateBps();
    stats.opusBitratePreset = localConfig.opusBitrateName();
    stats.opusBitrateLimited = false;
    stats.latencyMode = localConfig.latencyModeName();
    stats.latencyTarget = localConfig.latencyTargetDescription();
    stats.opusFrameDurationMs = localConfig.opusFrameDurationMs();
    stats.framesSent = framesSent.load(std::memory_order_relaxed);
    stats.serverFifoUnderruns = serverFifoUnderruns.load(std::memory_order_relaxed);
    stats.fifoDroppedFrames = fifo.getDroppedFrames();
    stats.senderQueueFillFrames = fifo.getAvailableFrames();
    stats.senderQueueCapacityFrames = fifo.getCapacityFrames();
    if (localConfig.sessionSampleRate > 1.0)
        stats.senderQueueFillMs = (static_cast<double> (stats.senderQueueFillFrames) / localConfig.sessionSampleRate) * 1000.0;

    webrtcSender.fillStats(stats);

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
