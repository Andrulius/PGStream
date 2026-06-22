#include "NetworkServer.h"
#include "HtmlAssets.h"
#include "SelfSignedCertificate.h"
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

bool hasJsonProperty(const juce::var& object, const char* name)
{
    if (auto* dynamic = object.getDynamicObject())
        return dynamic->hasProperty(name);

    return false;
}

bool isValidBitrateBps(int bps)
{
    return bps == 128000 || bps == 192000 || bps == 256000 || bps == 320000 || bps == 510000;
}

bool isValidLatencyText(const juce::String& text)
{
    const auto value = text.toLowerCase();
    return value.contains("safe")
        || value.contains("medium")
        || value.contains("ultra")
        || value.contains("low")
        || value.contains("balanced");
}

bool isValidAutoModeText(const juce::String& text)
{
    const auto value = text.toLowerCase();
    return value == "balanced" || value == "quality" || value == "latency";
}

void sendHttpOkHeaders(mg_connection* connection, const char* mimeType, size_t size)
{
    auto header = juce::String("HTTP/1.1 200 OK\r\nContent-Type: ")
        + mimeType
        + "\r\nContent-Length: "
        + juce::String(static_cast<int64_t> (size))
        + "\r\nCache-Control: no-store"
        + "\r\nCross-Origin-Opener-Policy: same-origin"
        + "\r\nCross-Origin-Embedder-Policy: require-corp"
        + "\r\nCross-Origin-Resource-Policy: same-origin"
        + "\r\n\r\n";

    mg_write(connection, header.toRawUTF8(), header.getNumBytesAsUTF8());
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

    if (preset.contains("very"))
        return LatencyMode::verySafe;
    if (preset.contains("safe"))
        return LatencyMode::safe;
    if (preset.contains("ultra"))
        return LatencyMode::ultraLowExperimental;
    if (preset.contains("low"))
        return LatencyMode::lowLatency;
    if (preset.contains("medium") || preset.contains("balanced"))
        return LatencyMode::medium;

    return fallback;
}

TransportMode parseBrowserTransportMode(const juce::var& message, TransportMode fallback)
{
    const auto value = jsonPropertyString(message, "transportMode").toLowerCase();

    if (value.contains("pcm16") || value.contains("pcm-16"))
        return TransportMode::pcm16DataChannel;
    if (value.contains("pcm24") || value.contains("pcm-24"))
        return TransportMode::pcm24DataChannel;
    if (value.contains("pcm32") || value.contains("pcm-32") || value.contains("float"))
        return TransportMode::pcm32FloatDataChannel;
    if (value.contains("auto"))
        return TransportMode::autoSelect;
    if (value.contains("opus"))
        return TransportMode::opusWebRtc;

    return fallback;
}

juce::String nowTimestamp()
{
    return juce::Time::getCurrentTime().toISO8601(true);
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

void NetworkServer::setActiveProfileCallback(std::function<void(OpusBitrateMode, LatencyMode, TransportMode)> callback)
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
    auto changed = false;
    {
        std::lock_guard<std::mutex> lock(configMutex);
        needsRestart = contextRunning.load(std::memory_order_acquire)
            && adjustedConfig.streamEnabled
            && (adjustedConfig.port != selectedConfig.port
                || adjustedConfig.useSelfSignedCertificate != selectedConfig.useSelfSignedCertificate);
        changed = stateRevision == 0
            || adjustedConfig.opusBitrateMode != config.opusBitrateMode
            || adjustedConfig.latencyMode != config.latencyMode
            || adjustedConfig.port != config.port
            || adjustedConfig.selectedTransportMode != config.selectedTransportMode
            || adjustedConfig.transportMode != config.transportMode
            || adjustedConfig.useSelfSignedCertificate != config.useSelfSignedCertificate
            || adjustedConfig.keepAliveWhenIdle != config.keepAliveWhenIdle
            || adjustedConfig.audioPassthrough != config.audioPassthrough;
        selectedConfig = adjustedConfig;
        config = adjustedConfig;
        if (changed)
            noteStateChangeLocked(stateRevision == 0 ? "startup" : "plugin_user",
                                  stateRevision == 0 ? "initial active state" : "plugin parameter change");
    }

    if (changed)
        broadcastStateUpdate(getStats());

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

    const auto networkSelection = selectBestLanInterface(newConfig.port, newConfig.usesHttps());
    CertificateFiles certificate;
    if (newConfig.usesHttps())
    {
        certificate = ensureSelfSignedCertificate(networkSelection.primaryAddress);
        if (! certificate.ready)
        {
            updateStatus("certificate error: " + certificate.description);
            contextRunning.store(false, std::memory_order_release);
            return false;
        }
    }

    const auto portString = juce::String(newConfig.port) + (newConfig.usesHttps() ? "s" : "");
    const auto certificatePath = certificate.pemFile.getFullPathName();

    std::vector<const char*> options;
    options.reserve(24);
    options.push_back("listening_ports");
    options.push_back(portString.toRawUTF8());
    if (newConfig.usesHttps())
    {
        options.push_back("ssl_certificate");
        options.push_back(certificatePath.toRawUTF8());
    }
    options.push_back("num_threads");
    options.push_back("4");
    options.push_back("enable_websocket_ping_pong");
    options.push_back("yes");
    options.push_back("websocket_timeout_ms");
    options.push_back("30000");
    options.push_back("request_timeout_ms");
    options.push_back("1500");
    options.push_back("linger_timeout_ms");
    options.push_back("0");
    options.push_back("authentication_domain");
    options.push_back("PGStream");
    options.push_back(nullptr);

    context = mg_start(&callbacks, this, options.data());
    if (context == nullptr)
    {
        updateStatus("port bind or server start failed");
        contextRunning.store(false, std::memory_order_release);
        return false;
    }

    mg_set_request_handler(context, "/", &NetworkServer::requestHandler, this);
    mg_set_request_handler(context, "/index.html", &NetworkServer::requestHandler, this);
    mg_set_request_handler(context, "/app.js", &NetworkServer::requestHandler, this);
    mg_set_request_handler(context, "/pcm-worklet.js", &NetworkServer::requestHandler, this);
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

    refreshLanSelection(newConfig.port, newConfig.usesHttps());
    updateStatus(newConfig.usesHttps() ? "streaming over HTTPS" : "streaming over HTTP");

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

    {
        std::lock_guard<std::mutex> lock(websocketMutex);
        websocketConnections.clear();
    }

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
        const auto webRtcFrameFrames = juce::jmax<size_t> (1, webrtcSender.audioFrameCount());
        const auto webRtcFrameDurationMs = juce::jmax(1, static_cast<int> (std::round(static_cast<double> (webRtcFrameFrames) * 1000.0 / 48000.0)));
        const auto webRtcTracks = webrtcSender.openOutputCount();
        const auto nowMs = juce::Time::getMillisecondCounterHiRes();

        if (nowMs >= nextLanRefreshMs)
        {
            refreshLanSelection(localConfig.port, localConfig.usesHttps());
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
        const auto profileSource = jsonPropertyString(message, "profileSource").toLowerCase();
        const auto autoOrigin = profileSource == "auto" || profileSource == "autonegotiation";
        const auto origin = autoOrigin
            ? "autonegotiation"
            : "browser_user";
        OpusBitrateMode activeBitrateMode;
        LatencyMode activeLatencyMode;
        TransportMode selectedTransportMode;
        TransportMode activeTransportMode;
        {
            std::lock_guard<std::mutex> lock(configMutex);
            activeBitrateMode = parseBrowserBitrateMode(message, config.opusBitrateMode);
            activeLatencyMode = parseBrowserLatencyMode(message, config.latencyMode);
            selectedTransportMode = parseBrowserTransportMode(message, config.selectedTransportMode);
            if (! config.useSelfSignedCertificate && selectedTransportMode != TransportMode::opusWebRtc)
                selectedTransportMode = TransportMode::opusWebRtc;
            activeTransportMode = StreamConfig::resolveTransportMode(selectedTransportMode, config.useSelfSignedCertificate);
            changed = activeBitrateMode != config.opusBitrateMode
                || activeLatencyMode != config.latencyMode
                || selectedTransportMode != config.selectedTransportMode
                || activeTransportMode != config.transportMode
                || (autoOrigin && hasJsonProperty(message, "autoMode")
                    && jsonPropertyString(message, "autoMode") != autonegotiationMode);

            if (changed)
            {
                config.opusBitrateMode = activeBitrateMode;
                config.latencyMode = activeLatencyMode;
                config.selectedTransportMode = selectedTransportMode;
                config.transportMode = activeTransportMode;
                selectedConfig.opusBitrateMode = activeBitrateMode;
                selectedConfig.latencyMode = activeLatencyMode;
                selectedConfig.selectedTransportMode = selectedTransportMode;
                selectedConfig.transportMode = activeTransportMode;
                if (autoOrigin)
                {
                    const auto requestedAutoMode = jsonPropertyString(message, "autoMode");
                    if (isValidAutoModeText(requestedAutoMode))
                        autonegotiationMode = requestedAutoMode;
                    autonegotiationState = "running";
                }
                noteStateChangeLocked(origin, autoOrigin
                    ? "auto negotiation selected active profile"
                    : "browser selected active profile");
            }
        }

        if (changed)
        {
            notifyActiveProfileChanged(activeBitrateMode, activeLatencyMode, selectedTransportMode);
            StreamConfig updatedConfig;
            {
                std::lock_guard<std::mutex> lock(configMutex);
                updatedConfig = config;
            }
            webrtcSender.applyConfig(updatedConfig);
        }

        webrtcSender.handleOffer(connection, message);
        sendStateUpdate(connection, getStats());
        return;
    }

    if (type == "stream_control_request")
    {
        const auto requestId = jsonPropertyString(message, "requestId");
        const auto requestedBitrate = jsonPropertyInt(message, "bitrateBps", 0);
        const auto requestedLatency = jsonPropertyString(message, hasJsonProperty(message, "latencyMode") ? "latencyMode" : "latencyPreset");
        const auto requestedAutoMode = jsonPropertyString(message, hasJsonProperty(message, "autoMode") ? "autoMode" : "autonegotiationMode");
        const auto reject = [this, connection, requestId] (const char* reason)
        {
            const auto stats = getStats();
            const auto json = juce::String("{\"type\":\"stream_control_rejected\",\"requestId\":")
                + jsonString(requestId)
                + ",\"reason\":"
                + jsonString(reason)
                + ",\"currentStateRevision\":"
                + juce::String(stats.stateRevision)
                + "}";
            mg_websocket_write(connection, MG_WEBSOCKET_OPCODE_TEXT, json.toRawUTF8(), json.getNumBytesAsUTF8());
            sendStateUpdate(connection, stats);
        };

        if (hasJsonProperty(message, "bitrateBps") && ! isValidBitrateBps(requestedBitrate))
        {
            reject("invalid bitrate");
            return;
        }

        if ((hasJsonProperty(message, "latencyMode") || hasJsonProperty(message, "latencyPreset"))
            && ! isValidLatencyText(requestedLatency))
        {
            reject("invalid latency");
            return;
        }

        if ((hasJsonProperty(message, "autoMode") || hasJsonProperty(message, "autonegotiationMode"))
            && ! isValidAutoModeText(requestedAutoMode))
        {
            reject("invalid auto mode");
            return;
        }

        auto changed = false;
        StreamConfig updatedConfig;
        OpusBitrateMode activeBitrateMode;
        LatencyMode activeLatencyMode;
        TransportMode selectedTransportMode;
        TransportMode activeTransportMode;
        {
            std::lock_guard<std::mutex> lock(configMutex);
            activeBitrateMode = parseBrowserBitrateMode(message, config.opusBitrateMode);
            activeLatencyMode = parseBrowserLatencyMode(message, config.latencyMode);
            selectedTransportMode = hasJsonProperty(message, "transportMode")
                ? parseBrowserTransportMode(message, config.selectedTransportMode)
                : config.selectedTransportMode;
            if (! config.useSelfSignedCertificate && selectedTransportMode != TransportMode::opusWebRtc)
                selectedTransportMode = TransportMode::opusWebRtc;
            activeTransportMode = StreamConfig::resolveTransportMode(selectedTransportMode, config.useSelfSignedCertificate);
            changed = activeBitrateMode != config.opusBitrateMode
                || activeLatencyMode != config.latencyMode
                || selectedTransportMode != config.selectedTransportMode
                || activeTransportMode != config.transportMode
                || ((hasJsonProperty(message, "autoMode") || hasJsonProperty(message, "autonegotiationMode"))
                    && requestedAutoMode != autonegotiationMode);
            if (changed)
            {
                config.opusBitrateMode = activeBitrateMode;
                config.latencyMode = activeLatencyMode;
                config.selectedTransportMode = selectedTransportMode;
                config.transportMode = activeTransportMode;
                selectedConfig.opusBitrateMode = activeBitrateMode;
                selectedConfig.latencyMode = activeLatencyMode;
                selectedConfig.selectedTransportMode = selectedTransportMode;
                selectedConfig.transportMode = activeTransportMode;
                if (hasJsonProperty(message, "autoMode") || hasJsonProperty(message, "autonegotiationMode"))
                {
                    autonegotiationMode = requestedAutoMode;
                    autonegotiationState = jsonPropertyString(message, "autonegotiationState");
                    if (autonegotiationState.isEmpty())
                        autonegotiationState = "selected";
                }
                noteStateChangeLocked("browser_user", "browser control request");
            }
            updatedConfig = config;
        }

        if (changed)
        {
            notifyActiveProfileChanged(activeBitrateMode, activeLatencyMode, selectedTransportMode);
            webrtcSender.applyConfig(updatedConfig);
            broadcastStateUpdate(getStats());
        }
        else
        {
            juce::ignoreUnused(requestId);
            sendStateUpdate(connection, getStats());
        }
        return;
    }

    if (type == "browser_receiver_stats")
    {
        webrtcSender.updateReceiverStats(connection, message);
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

void NetworkServer::notifyActiveProfileChanged(OpusBitrateMode bitrateMode, LatencyMode latencyMode, TransportMode transportMode)
{
    std::function<void(OpusBitrateMode, LatencyMode, TransportMode)> callback;
    {
        std::lock_guard<std::mutex> lock(callbackMutex);
        callback = activeProfileCallback;
    }

    if (callback)
        callback(bitrateMode, latencyMode, transportMode);
}

void NetworkServer::noteStateChangeLocked(const char* origin, const char* reason)
{
    ++stateRevision;
    stateOrigin = origin;
    lastAdaptationReason = reason;
    lastAdaptationTimestamp = nowTimestamp();
}

void NetworkServer::sendStateUpdate(mg_connection* connection, const StreamStats& stats)
{
    if (connection == nullptr)
        return;

    {
        std::lock_guard<std::mutex> lock(configMutex);
        lastStateUpdateSentTimestamp = nowTimestamp();
    }

    juce::String json;
    json.preallocateBytes(1024);
    json += "{\"type\":\"stream_state_update\",\"stateRevision\":";
    json += juce::String(stats.stateRevision);
    json += ",\"origin\":";
    json += jsonString(stats.stateOrigin);
    json += ",\"activeBitrateBps\":";
    json += juce::String(stats.opusBitrateBps);
    json += ",\"activeBitrateLabel\":";
    json += jsonString(stats.opusBitratePreset);
    json += ",\"activeLatencyMode\":";
    json += jsonString(stats.latencyMode);
    json += ",\"activeFrameDurationMs\":";
    json += juce::String(stats.opusFrameDurationMs);
    json += ",\"activePlayoutDelayHintMs\":";
    json += juce::String(stats.activePlayoutDelayHintMs);
    json += ",\"transportMode\":";
    json += jsonString(stats.transportMode);
    json += ",\"selectedTransportMode\":";
    json += jsonString(stats.selectedTransportMode);
    json += ",\"serverScheme\":";
    json += jsonString(stats.serverScheme);
    json += ",\"httpsEnabled\":";
    json += stats.httpsEnabled ? "true" : "false";
    json += ",\"autonegotiationMode\":";
    json += jsonString(stats.autonegotiationMode);
    json += ",\"autonegotiationEnabled\":";
    json += stats.autonegotiationEnabled ? "true" : "false";
    json += ",\"autonegotiationState\":";
    json += jsonString(stats.autonegotiationState);
    json += ",\"lastAdaptationReason\":";
    json += jsonString(stats.lastAdaptationReason);
    json += ",\"lastAdaptationTimestamp\":";
    json += jsonString(stats.lastAdaptationTimestamp);
    json += "}";

    mg_websocket_write(connection,
                       MG_WEBSOCKET_OPCODE_TEXT,
                       json.toRawUTF8(),
                       json.getNumBytesAsUTF8());
}

void NetworkServer::broadcastStateUpdate(const StreamStats& stats)
{
    std::vector<mg_connection*> connections;
    {
        std::lock_guard<std::mutex> lock(websocketMutex);
        connections.assign(websocketConnections.begin(), websocketConnections.end());
    }

    for (auto* connection : connections)
        sendStateUpdate(connection, stats);
}

void NetworkServer::refreshLanSelection(int port, bool useHttps)
{
    const auto networkSelection = selectBestLanInterface(port, useHttps);

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
        sendHttpOkHeaders(connection, "application/json; charset=utf-8", std::strlen(json));
        mg_write(connection, json, std::strlen(json));
        return 200;
    }

    if (std::strcmp(uri, "/info") == 0)
    {
        const auto stats = getStats();

        juce::String json;
        json.preallocateBytes(4096);
        json += "{\"name\":\"PGStream\",\"transport\":";
        json += jsonString(stats.transportMode);
        json += ",\"selectedTransport\":";
        json += jsonString(stats.selectedTransportMode);
        json += ",\"serverScheme\":";
        json += jsonString(stats.serverScheme);
        json += ",\"httpsEnabled\":";
        json += stats.httpsEnabled ? "true" : "false";
        json += ",\"selfSignedCertificateEnabled\":";
        json += stats.selfSignedCertificateEnabled ? "true" : "false";
        json += ",\"audioPassthrough\":";
        json += stats.audioPassthrough ? "true" : "false";
        json += ",\"pcmBitsPerSample\":";
        json += juce::String(stats.pcmBitsPerSample);
        json += ",\"pcmPacketFrames\":";
        json += juce::String(stats.pcmPacketFrames);
        json += ",\"pcmTargetBufferMs\":";
        json += juce::String(stats.pcmTargetBufferMs);
        json += ",\"opusCodec\":\"opus/48000/2\",\"opusBitrateBps\":";
        json += juce::String(stats.opusBitrateBps);
        json += ",\"opusBitratePreset\":";
        json += jsonString(stats.opusBitratePreset);
        json += ",\"selectedOpusBitrateBps\":";
        json += juce::String(stats.selectedOpusBitrateBps);
        json += ",\"selectedOpusBitratePreset\":";
        json += jsonString(stats.selectedOpusBitratePreset);
        json += ",\"opusBitrateLimited\":";
        json += stats.opusBitrateLimited ? "true" : "false";
        json += ",\"latencyPreset\":";
        json += jsonString(stats.latencyMode);
        json += ",\"selectedLatencyPreset\":";
        json += jsonString(stats.selectedLatencyMode);
        json += ",\"latencyTarget\":";
        json += jsonString(stats.latencyTarget);
        json += ",\"opusFrameDurationMs\":";
        json += juce::String(stats.opusFrameDurationMs);
        json += ",\"activePlayoutDelayHintMs\":";
        json += juce::String(stats.activePlayoutDelayHintMs);
        json += ",\"stateRevision\":";
        json += juce::String(stats.stateRevision);
        json += ",\"stateOrigin\":";
        json += jsonString(stats.stateOrigin);
        json += ",\"lastAdaptationReason\":";
        json += jsonString(stats.lastAdaptationReason);
        json += ",\"lastAdaptationTimestamp\":";
        json += jsonString(stats.lastAdaptationTimestamp);
        json += ",\"autonegotiationMode\":";
        json += jsonString(stats.autonegotiationMode);
        json += ",\"autonegotiationEnabled\":";
        json += stats.autonegotiationEnabled ? "true" : "false";
        json += ",\"autonegotiationState\":";
        json += jsonString(stats.autonegotiationState);
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
        json += ",\"webrtcEncodeOverBudgetCount\":";
        json += juce::String(stats.webrtcEncodeOverBudgetCount);
        json += ",\"webrtcEncoderOverloadWarnings\":";
        json += juce::String(stats.webrtcEncoderOverloadWarnings);
        json += ",\"webrtcNegotiatedPayloadType\":";
        json += juce::String(stats.webrtcNegotiatedPayloadType);
        json += ",\"webrtcActualPayloadType\":";
        json += juce::String(stats.webrtcActualPayloadType);
        json += ",\"webrtcSsrc\":";
        json += juce::String(static_cast<int64_t> (stats.webrtcSsrc));
        json += ",\"webrtcSequenceCurrent\":";
        json += juce::String(static_cast<int> (stats.webrtcSequenceCurrent));
        json += ",\"webrtcTimestampCurrent\":";
        json += juce::String(static_cast<int64_t> (stats.webrtcTimestampCurrent));
        json += ",\"webrtcTimestampIncrementExpected\":";
        json += juce::String(static_cast<int64_t> (stats.webrtcTimestampIncrementExpected));
        json += ",\"webrtcTimestampIncrementActual\":";
        json += juce::String(static_cast<int64_t> (stats.webrtcTimestampIncrementActual));
        json += ",\"webrtcTimestampAnomalyCount\":";
        json += juce::String(stats.webrtcTimestampAnomalyCount);
        json += ",\"webrtcPacketsSubmittedToTrack\":";
        json += juce::String(stats.webrtcPacketsSubmittedToTrack);
        json += ",\"webrtcBytesSubmittedToTrack\":";
        json += juce::String(stats.webrtcBytesSubmittedToTrack);
        json += ",\"webrtcSubmitErrors\":";
        json += juce::String(stats.webrtcSubmitErrors);
        json += ",\"pcmOpenChannels\":";
        json += juce::String(stats.pcmOpenChannels);
        json += ",\"pcmReceiverReadyCount\":";
        json += juce::String(stats.pcmReceiverReadyCount);
        json += ",\"pcmPacketsSent\":";
        json += juce::String(stats.pcmPacketsSent);
        json += ",\"pcmBytesSent\":";
        json += juce::String(stats.pcmBytesSent);
        json += ",\"pcmSendCalls\":";
        json += juce::String(stats.pcmSendCalls);
        json += ",\"pcmSendFailures\":";
        json += juce::String(stats.pcmSendFailures);
        json += ",\"pcmDroppedBeforeSend\":";
        json += juce::String(stats.pcmDroppedBeforeSend);
        json += ",\"pcmDataChannelBufferedBytes\":";
        json += juce::String(static_cast<int64_t> (stats.pcmDataChannelBufferedBytes));
        json += ",\"pcmSequenceCurrent\":";
        json += juce::String(stats.pcmSequenceCurrent);
        json += ",\"pcmSampleCursor\":";
        json += juce::String(stats.pcmSampleCursor);
        json += ",\"pcmReceiverStatsCount\":";
        json += juce::String(stats.pcmReceiverStatsCount);
        json += ",\"pcmReceiverBufferMs\":";
        json += juce::String(stats.pcmReceiverBufferMs, 2);
        json += ",\"pcmReceiverAckAgeMs\":";
        json += juce::String(stats.pcmReceiverAckAgeMs, 2);
        json += ",\"pcmReceiverUnderflows\":";
        json += juce::String(stats.pcmReceiverUnderflows);
        json += ",\"pcmReceiverOverflows\":";
        json += juce::String(stats.pcmReceiverOverflows);
        json += ",\"pcmReceiverMissingPackets\":";
        json += juce::String(stats.pcmReceiverMissingPackets);
        json += ",\"pcmReceiverLatePackets\":";
        json += juce::String(stats.pcmReceiverLatePackets);
        json += ",\"pcmAudioContextSampleRate\":";
        json += juce::String(stats.pcmAudioContextSampleRate, 2);
        json += ",\"pcmAudioContextBaseLatencyMs\":";
        json += juce::String(stats.pcmAudioContextBaseLatencyMs, 3);
        json += ",\"pcmAudioContextOutputLatencyMs\":";
        json += juce::String(stats.pcmAudioContextOutputLatencyMs, 3);
        json += ",\"pcmReceiverLastError\":";
        json += jsonString(stats.pcmReceiverLastError);
        json += ",\"opusEncodeErrors\":";
        json += juce::String(stats.opusEncodeErrors);
        json += ",\"opusPacketBytesLast\":";
        json += juce::String(stats.opusPacketBytesLast);
        json += ",\"opusPacketBytesAvg\":";
        json += juce::String(stats.opusPacketBytesAvg, 1);
        json += ",\"inputRmsL\":";
        json += juce::String(stats.inputRmsL, 6);
        json += ",\"inputRmsR\":";
        json += juce::String(stats.inputRmsR, 6);
        json += ",\"stateRevision\":";
        json += juce::String(stats.stateRevision);
        json += ",\"stateOrigin\":";
        json += jsonString(stats.stateOrigin);
        json += ",\"lastStateUpdateSentTimestamp\":";
        json += jsonString(stats.lastStateUpdateSentTimestamp);
        json += ",\"status\":";
        json += jsonString(stats.statusText);
        json += ",\"lanUrl\":";
        json += jsonString(stats.lanUrl);
        json += ",\"candidateLanUrls\":";
        json += jsonStringArray(stats.candidateLanUrls);
        json += "}}";

        sendHttpOkHeaders(connection, "application/json; charset=utf-8", json.getNumBytesAsUTF8());
        mg_write(connection, json.toRawUTF8(), json.getNumBytesAsUTF8());
        return 200;
    }

    HtmlAsset asset;
    if (getHtmlAsset(uri, asset))
    {
        sendHttpOkHeaders(connection, asset.mimeType, asset.size);
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
    auto* server = static_cast<NetworkServer*> (cbdata);
    {
        std::lock_guard<std::mutex> lock(server->websocketMutex);
        server->websocketConnections.insert(connection);
    }

    server->sendStateUpdate(connection, server->getStats());
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
    {
        std::lock_guard<std::mutex> lock(server->websocketMutex);
        server->websocketConnections.erase(mutableConnection);
    }
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
    StreamConfig localSelectedConfig;
    uint64_t localStateRevision = 0;
    juce::String localStateOrigin;
    juce::String localLastAdaptationReason;
    juce::String localLastAdaptationTimestamp;
    juce::String localLastStateUpdateSentTimestamp;
    juce::String localAutonegotiationMode;
    juce::String localAutonegotiationState;
    {
        std::lock_guard<std::mutex> lock(configMutex);
        localSelectedConfig = selectedConfig;
        localConfig = config;
        localStateRevision = stateRevision;
        localStateOrigin = stateOrigin;
        localLastAdaptationReason = lastAdaptationReason;
        localLastAdaptationTimestamp = lastAdaptationTimestamp;
        localLastStateUpdateSentTimestamp = lastStateUpdateSentTimestamp;
        localAutonegotiationMode = autonegotiationMode;
        localAutonegotiationState = autonegotiationState;
    }

    StreamStats stats;
    stats.serverRunning = contextRunning.load(std::memory_order_acquire);
    stats.streamEnabled = enabled.load(std::memory_order_acquire);
    stats.port = localConfig.port;
    stats.connectedClients = webrtcSender.peerCount();
    stats.transportMode = localConfig.transportModeName();
    stats.inputSampleRate = juce::jmax(1, static_cast<int> (std::round(localConfig.sessionSampleRate)));
    stats.selectedOpusBitrateBps = localSelectedConfig.opusBitrateBps();
    stats.selectedOpusBitratePreset = localSelectedConfig.opusBitrateName();
    stats.opusBitrateBps = localConfig.opusBitrateBps();
    stats.opusBitratePreset = localConfig.opusBitrateName();
    stats.opusBitrateLimited = false;
    stats.selectedLatencyMode = localSelectedConfig.latencyModeName();
    stats.latencyMode = localConfig.latencyModeName();
    stats.latencyTarget = localConfig.latencyTargetDescription();
    stats.opusFrameDurationMs = localConfig.opusFrameDurationMs();
    stats.activePlayoutDelayHintMs = localConfig.playoutDelayHintMs();
    stats.stateRevision = localStateRevision;
    stats.stateOrigin = localStateOrigin;
    stats.lastAdaptationReason = localLastAdaptationReason;
    stats.lastAdaptationTimestamp = localLastAdaptationTimestamp;
    stats.lastUserRequestedBitratePreset = localSelectedConfig.opusBitrateName();
    stats.lastUserRequestedLatencyMode = localSelectedConfig.latencyModeName();
    stats.lastStateUpdateSentTimestamp = localLastStateUpdateSentTimestamp;
    stats.autonegotiationMode = localAutonegotiationMode;
    stats.autonegotiationEnabled = localAutonegotiationState != "inactive";
    stats.autonegotiationState = localAutonegotiationState;
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
