#include "WebRtcAudioSender.h"
#include <civetweb.h>
#include <opus.h>
#include <rtc/rtc.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <random>
#include <sstream>

namespace pgstream
{
namespace
{
constexpr int opusSampleRate = 48000;
constexpr int opusChannels = 2;
constexpr int defaultOpusPayloadType = 111;
constexpr int maxRtpPayloadType = 127;

juce::String jsonString(const juce::String& value)
{
    auto escaped = value.replace("\\", "\\\\")
                        .replace("\"", "\\\"")
                        .replace("\r", "\\r")
                        .replace("\n", "\\n");
    return "\"" + escaped + "\"";
}

juce::var property(const juce::var& object, const char* name)
{
    if (auto* dynamic = object.getDynamicObject())
        return dynamic->getProperty(name);

    return {};
}

juce::String propertyString(const juce::var& object, const char* name)
{
    return property(object, name).toString();
}

int propertyInt(const juce::var& object, const char* name, int fallback)
{
    const auto value = property(object, name);
    if (value.isInt() || value.isInt64() || value.isDouble() || value.isBool())
        return static_cast<int> (value);

    const auto text = value.toString();
    return text.isNotEmpty() ? text.getIntValue() : fallback;
}

OpusBitrateMode parseBitrateMode(const juce::var& message, OpusBitrateMode fallback)
{
    const auto preset = propertyString(message, "bitratePreset").toLowerCase();
    const auto bps = propertyInt(message, "bitrateBps", 0);

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

LatencyMode parseLatencyMode(const juce::var& message, LatencyMode fallback)
{
    const auto preset = propertyString(message, "latencyPreset").toLowerCase();

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

uint32_t nextSsrc()
{
    static std::atomic<uint32_t> counter { 0x4f120000u };
    return counter.fetch_add(1, std::memory_order_relaxed) + 1u;
}

std::string opusFmtp(const StreamConfig& config)
{
    return "minptime=10;stereo=1;sprop-stereo=1;maxaveragebitrate="
        + std::to_string(config.opusBitrateBps())
        + ";useinbandfec=0;usedtx=0";
}

std::string toStdString(const juce::String& value)
{
    return value.toStdString();
}

struct OfferAudioInfo
{
    juce::String mid { "audio" };
    int opusPayloadType = defaultOpusPayloadType;
};

OfferAudioInfo parseOfferAudioInfo(const juce::String& sdp)
{
    OfferAudioInfo result;
    auto inAudioSection = false;

    juce::StringArray lines;
    lines.addLines(sdp);

    for (auto line : lines)
    {
        line = line.trimCharactersAtEnd("\r").trim();
        if (line.startsWith("m="))
        {
            inAudioSection = line.startsWith("m=audio ");
            continue;
        }

        if (! inAudioSection)
            continue;

        if (line.startsWith("a=mid:"))
        {
            const auto mid = line.fromFirstOccurrenceOf("a=mid:", false, false).trim();
            if (mid.isNotEmpty())
                result.mid = mid;
            continue;
        }

        if (line.startsWith("a=rtpmap:") && line.containsIgnoreCase(" opus/48000"))
        {
            const auto payloadText = line.fromFirstOccurrenceOf("a=rtpmap:", false, false)
                                         .upToFirstOccurrenceOf(" ", false, false)
                                         .trim();
            const auto payloadType = payloadText.getIntValue();
            if (payloadType > 0 && payloadType <= maxRtpPayloadType)
                result.opusPayloadType = payloadType;
        }
    }

    return result;
}
}

struct WebRtcAudioSender::Peer
{
    mg_connection* connection = nullptr;
    std::shared_ptr<rtc::PeerConnection> pc;
    std::shared_ptr<rtc::Track> track;
    std::shared_ptr<rtc::RtpPacketizationConfig> rtpConfig;
    std::atomic<bool> open { false };
    std::atomic<int> state { static_cast<int> (rtc::PeerConnection::State::New) };
    std::atomic<int> iceState { static_cast<int> (rtc::PeerConnection::IceState::New) };
};

WebRtcAudioSender::WebRtcAudioSender()
    : encoder(nullptr, [] (OpusEncoder* encoderToDestroy)
      {
          if (encoderToDestroy != nullptr)
              opus_encoder_destroy(encoderToDestroy);
      })
{
    opusPacket.resize(4096);
}

WebRtcAudioSender::~WebRtcAudioSender()
{
    clear();
}

void WebRtcAudioSender::applyConfig(const StreamConfig& newConfig)
{
    std::lock_guard<std::mutex> lock(mutex);
    config = newConfig;
    configureEncoderIfNeeded();
}

void WebRtcAudioSender::configureEncoderIfNeeded()
{
    const auto targetBitrate = config.opusBitrateBps();
    const auto targetFrameMs = config.opusFrameDurationMs();

    if (encoder != nullptr && encoderBitrateBps == targetBitrate && encoderFrameMs == targetFrameMs)
        return;

    recreateEncoder();
}

void WebRtcAudioSender::recreateEncoder()
{
    int error = OPUS_OK;
    std::unique_ptr<OpusEncoder, void (*)(OpusEncoder*)> newEncoder(
        opus_encoder_create(opusSampleRate, opusChannels, OPUS_APPLICATION_AUDIO, &error),
        [] (OpusEncoder* encoderToDestroy)
        {
            if (encoderToDestroy != nullptr)
                opus_encoder_destroy(encoderToDestroy);
        });

    if (error != OPUS_OK || newEncoder == nullptr)
        return;

    const auto targetBitrate = config.opusBitrateBps();
    const auto targetFrameMs = config.opusFrameDurationMs();

    opus_encoder_ctl(newEncoder.get(), OPUS_SET_BITRATE(targetBitrate));
    opus_encoder_ctl(newEncoder.get(), OPUS_SET_SIGNAL(OPUS_SIGNAL_MUSIC));
    opus_encoder_ctl(newEncoder.get(), OPUS_SET_DTX(0));
    opus_encoder_ctl(newEncoder.get(), OPUS_SET_INBAND_FEC(0));
    opus_encoder_ctl(newEncoder.get(), OPUS_SET_COMPLEXITY(8));
    opus_encoder_ctl(newEncoder.get(), OPUS_SET_VBR(0));

    encoder = std::move(newEncoder);
    encoderBitrateBps = targetBitrate;
    encoderFrameMs = targetFrameMs;
    encoderFrameFrames = static_cast<size_t> (opusSampleRate * targetFrameMs / 1000);
    encoderFrameFrames = juce::jmax<size_t> (120, encoderFrameFrames);
    pendingPcm.assign(encoderFrameFrames * opusChannels, 0.0f);
    pendingFrames = 0;
    encodedSampleCursor = 0;
}

void WebRtcAudioSender::resetCountersLocked()
{
    encodedFrames.store(0, std::memory_order_relaxed);
    encodedPackets.store(0, std::memory_order_relaxed);
    encodedBytes.store(0, std::memory_order_relaxed);
    sendCalls.store(0, std::memory_order_relaxed);
    rtpPacketsAttempted.store(0, std::memory_order_relaxed);
    rtpPacketsSent.store(0, std::memory_order_relaxed);
    rtpSendFailures.store(0, std::memory_order_relaxed);
    encoderOverloadWarnings.store(0, std::memory_order_relaxed);
}

void WebRtcAudioSender::handleOffer(mg_connection* connection, const juce::var& message)
{
    if (connection == nullptr)
        return;

    const auto sdp = propertyString(message, "sdp");
    if (sdp.isEmpty())
    {
        sendJson(connection, "{\"type\":\"webrtc-error\",\"message\":\"missing offer SDP\"}");
        return;
    }

    StreamConfig peerConfig;
    {
        std::lock_guard<std::mutex> lock(mutex);
        peerConfig = config;
        peerConfig.opusBitrateMode = parseBitrateMode(message, peerConfig.opusBitrateMode);
        peerConfig.latencyMode = parseLatencyMode(message, peerConfig.latencyMode);
        config.opusBitrateMode = peerConfig.opusBitrateMode;
        config.latencyMode = peerConfig.latencyMode;
        configureEncoderIfNeeded();
    }

    try
    {
        auto rtcConfig = rtc::Configuration();
        rtcConfig.forceMediaTransport = true;
        rtcConfig.enableIceTcp = false;

        auto peer = std::make_shared<Peer>();
        peer->connection = connection;
        peer->pc = std::make_shared<rtc::PeerConnection>(rtcConfig);

        std::weak_ptr<Peer> weakPeer = peer;

        peer->pc->onLocalDescription([this, weakPeer] (rtc::Description description)
        {
            if (auto locked = weakPeer.lock())
            {
                const auto json = juce::String("{\"type\":\"webrtc-answer\",\"sdpType\":")
                    + jsonString(description.typeString())
                    + ",\"sdp\":"
                    + jsonString(juce::String(std::string(description)))
                    + "}";
                sendJson(locked->connection, json);
            }
        });

        peer->pc->onLocalCandidate([this, weakPeer] (rtc::Candidate candidate)
        {
            if (auto locked = weakPeer.lock())
            {
                const auto json = juce::String("{\"type\":\"webrtc-candidate\",\"candidate\":")
                    + jsonString(juce::String(candidate.candidate()))
                    + ",\"mid\":"
                    + jsonString(juce::String(candidate.mid()))
                    + "}";
                sendJson(locked->connection, json);
            }
        });

        peer->pc->onStateChange([weakPeer] (rtc::PeerConnection::State state)
        {
            if (auto locked = weakPeer.lock())
            {
                locked->state.store(static_cast<int> (state), std::memory_order_release);
                if (state == rtc::PeerConnection::State::Disconnected
                    || state == rtc::PeerConnection::State::Failed
                    || state == rtc::PeerConnection::State::Closed)
                    locked->open.store(false, std::memory_order_release);
            }
        });

        peer->pc->onIceStateChange([weakPeer] (rtc::PeerConnection::IceState state)
        {
            if (auto locked = weakPeer.lock())
                locked->iceState.store(static_cast<int> (state), std::memory_order_release);
        });

        const auto offerAudio = parseOfferAudioInfo(sdp);
        const auto ssrc = nextSsrc();
        const auto cname = std::string("pgstream-audio");
        auto audio = rtc::Description::Audio(toStdString(offerAudio.mid), rtc::Description::Direction::SendOnly);
        audio.addOpusCodec(offerAudio.opusPayloadType, opusFmtp(peerConfig));
        audio.addSSRC(ssrc, cname, "pgstream", "pgstream-audio");

        peer->track = peer->pc->addTrack(audio);
        peer->rtpConfig = std::make_shared<rtc::RtpPacketizationConfig>(
            ssrc, cname, static_cast<uint8_t> (offerAudio.opusPayloadType), rtc::OpusRtpPacketizer::DefaultClockRate);

        auto packetizer = std::make_shared<rtc::OpusRtpPacketizer>(peer->rtpConfig);
        auto srReporter = std::make_shared<rtc::RtcpSrReporter>(peer->rtpConfig);
        packetizer->addToChain(srReporter);
        auto nackResponder = std::make_shared<rtc::RtcpNackResponder>();
        packetizer->addToChain(nackResponder);
        peer->track->setMediaHandler(packetizer);
        peer->track->onOpen([weakPeer]
        {
            if (auto locked = weakPeer.lock())
                locked->open.store(true, std::memory_order_release);
        });
        peer->track->onClosed([weakPeer]
        {
            if (auto locked = weakPeer.lock())
                locked->open.store(false, std::memory_order_release);
        });

        PeerPtr oldPeer;
        {
            std::lock_guard<std::mutex> lock(mutex);
            const auto it = peers.find(connection);
            if (it != peers.end())
            {
                oldPeer = it->second;
                peers.erase(it);
            }

            if (peers.empty())
                resetCountersLocked();

            peers[connection] = peer;
        }

        closePeer(oldPeer);

        peer->pc->setRemoteDescription(rtc::Description(toStdString(sdp), "offer"));
        peer->pc->setLocalDescription(rtc::Description::Type::Answer);
    }
    catch (const std::exception& exception)
    {
        sendJson(connection,
                 juce::String("{\"type\":\"webrtc-error\",\"message\":")
                    + jsonString(exception.what())
                    + "}");
    }
}

void WebRtcAudioSender::handleRemoteCandidate(mg_connection* connection, const juce::var& message)
{
    auto peer = findPeer(connection);
    if (peer == nullptr || peer->pc == nullptr)
        return;

    const auto candidate = propertyString(message, "candidate");
    if (candidate.isEmpty())
        return;

    const auto mid = propertyString(message, "mid");

    try
    {
        peer->pc->addRemoteCandidate(rtc::Candidate(toStdString(candidate), toStdString(mid)));
    }
    catch (...)
    {
        sendJson(connection, "{\"type\":\"webrtc-error\",\"message\":\"failed to add ICE candidate\"}");
    }
}

void WebRtcAudioSender::removeConnection(mg_connection* connection)
{
    PeerPtr peer;
    {
        std::lock_guard<std::mutex> lock(mutex);
        const auto it = peers.find(connection);
        if (it == peers.end())
            return;

        peer = it->second;
        peers.erase(it);
        if (peers.empty())
        {
            pendingFrames = 0;
            encodedSampleCursor = 0;
            resetCountersLocked();
        }
    }

    closePeer(peer);
}

void WebRtcAudioSender::clear()
{
    std::vector<PeerPtr> toClose;
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (const auto& entry : peers)
            toClose.push_back(entry.second);
        peers.clear();
        pendingFrames = 0;
        encodedSampleCursor = 0;
        resetCountersLocked();
    }

    for (auto& peer : toClose)
        closePeer(peer);
}

void WebRtcAudioSender::closePeer(PeerPtr peer)
{
    if (peer == nullptr)
        return;

    peer->open.store(false, std::memory_order_release);

    if (peer->track != nullptr)
        peer->track->close();

    if (peer->pc != nullptr)
    {
        peer->pc->resetCallbacks();
        peer->pc->close();
    }
}

WebRtcAudioSender::PeerPtr WebRtcAudioSender::findPeer(mg_connection* connection) const
{
    std::lock_guard<std::mutex> lock(mutex);
    const auto it = peers.find(connection);
    return it != peers.end() ? it->second : nullptr;
}

void WebRtcAudioSender::encodeAndSend(const float* interleavedStereo48k, size_t frameCount)
{
    if (interleavedStereo48k == nullptr || frameCount == 0)
        return;

    std::lock_guard<std::mutex> lock(mutex);

    const auto anyOpen = std::any_of(peers.begin(), peers.end(), [] (const auto& entry)
    {
        return entry.second != nullptr && entry.second->open.load(std::memory_order_acquire);
    });

    if (! anyOpen)
    {
        pendingFrames = 0;
        return;
    }

    configureEncoderIfNeeded();

    if (encoder == nullptr || encoderFrameFrames == 0)
        return;

    size_t sourceFrame = 0;
    while (sourceFrame < frameCount)
    {
        const auto writable = std::min(encoderFrameFrames - pendingFrames, frameCount - sourceFrame);
        std::copy(interleavedStereo48k + sourceFrame * opusChannels,
                  interleavedStereo48k + (sourceFrame + writable) * opusChannels,
                  pendingPcm.data() + pendingFrames * opusChannels);

        pendingFrames += writable;
        sourceFrame += writable;

        if (pendingFrames < encoderFrameFrames)
            continue;

        const auto encodeStarted = juce::Time::getMillisecondCounterHiRes();
        const auto encoded = opus_encode_float(encoder.get(),
                                               pendingPcm.data(),
                                               static_cast<int> (encoderFrameFrames),
                                               opusPacket.data(),
                                               static_cast<opus_int32> (opusPacket.size()));
        const auto encodeMs = juce::Time::getMillisecondCounterHiRes() - encodeStarted;

        if (encodeMs > static_cast<double> (encoderFrameMs) * 0.75)
            encoderOverloadWarnings.fetch_add(1, std::memory_order_relaxed);

        if (encoded > 0)
        {
            const auto sent = sendEncodedFrameLocked(opusPacket.data(),
                                                     static_cast<size_t> (encoded),
                                                     encodedSampleCursor);
            if (sent.sent > 0)
            {
                encodedFrames.fetch_add(encoderFrameFrames, std::memory_order_relaxed);
                encodedPackets.fetch_add(1, std::memory_order_relaxed);
                encodedBytes.fetch_add(static_cast<uint64_t> (encoded), std::memory_order_relaxed);
                encodedSampleCursor += encoderFrameFrames;
            }
        }

        pendingFrames = 0;
    }
}

WebRtcAudioSender::SendResult WebRtcAudioSender::sendEncodedFrameLocked(const uint8_t* data, size_t bytes, uint64_t frameStartSample)
{
    SendResult result;
    if (data == nullptr || bytes == 0)
        return result;

    const auto seconds = std::chrono::duration<double>(static_cast<double> (frameStartSample) / opusSampleRate);
    auto* byteData = reinterpret_cast<const rtc::byte*> (data);

    for (const auto& entry : peers)
    {
        const auto& peer = entry.second;
        if (peer == nullptr || peer->track == nullptr || ! peer->open.load(std::memory_order_acquire))
            continue;

        ++result.attempted;
        try
        {
            peer->track->sendFrame(byteData, bytes, rtc::FrameInfo(seconds));
            sendCalls.fetch_add(1, std::memory_order_relaxed);
            ++result.sent;
        }
        catch (...)
        {
            peer->open.store(false, std::memory_order_release);
            ++result.failed;
        }
    }

    if (result.attempted > 0)
        rtpPacketsAttempted.fetch_add(result.attempted, std::memory_order_relaxed);
    if (result.sent > 0)
        rtpPacketsSent.fetch_add(result.sent, std::memory_order_relaxed);
    if (result.failed > 0)
        rtpSendFailures.fetch_add(result.failed, std::memory_order_relaxed);

    return result;
}

void WebRtcAudioSender::resetAudio()
{
    std::lock_guard<std::mutex> lock(mutex);
    pendingFrames = 0;
    encodedSampleCursor = 0;
}

int WebRtcAudioSender::peerCount() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return static_cast<int> (peers.size());
}

int WebRtcAudioSender::openTrackCount() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return static_cast<int> (std::count_if(peers.begin(), peers.end(), [] (const auto& entry)
    {
        return entry.second != nullptr && entry.second->open.load(std::memory_order_acquire);
    }));
}

bool WebRtcAudioSender::hasOpenTrack() const
{
    return openTrackCount() > 0;
}

size_t WebRtcAudioSender::opusFrameCount() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return encoderFrameFrames;
}

void WebRtcAudioSender::fillStats(StreamStats& stats) const
{
    stats.webrtcEncodedFrames = encodedFrames.load(std::memory_order_relaxed);
    stats.webrtcEncodedPackets = encodedPackets.load(std::memory_order_relaxed);
    stats.webrtcEncodedBytes = encodedBytes.load(std::memory_order_relaxed);
    stats.webrtcSendCalls = sendCalls.load(std::memory_order_relaxed);
    stats.webrtcRtpPacketsAttempted = rtpPacketsAttempted.load(std::memory_order_relaxed);
    stats.webrtcRtpPacketsSent = rtpPacketsSent.load(std::memory_order_relaxed);
    stats.webrtcRtpSendFailures = rtpSendFailures.load(std::memory_order_relaxed);
    stats.webrtcEncoderOverloadWarnings = encoderOverloadWarnings.load(std::memory_order_relaxed);

    PeerPtr newestPeer;
    {
        std::lock_guard<std::mutex> lock(mutex);
        stats.opusBitrateBps = config.opusBitrateBps();
        stats.opusBitratePreset = config.opusBitrateName();
        stats.opusBitrateLimited = false;
        stats.latencyMode = config.latencyModeName();
        stats.latencyTarget = config.latencyTargetDescription();
        stats.opusFrameDurationMs = config.opusFrameDurationMs();
        stats.transportMode = config.transportModeName();
        stats.webrtcPeerCount = static_cast<int> (peers.size());
        stats.webrtcOpenTracks = static_cast<int> (std::count_if(peers.begin(), peers.end(), [] (const auto& entry)
        {
            return entry.second != nullptr && entry.second->open.load(std::memory_order_acquire);
        }));

        if (! peers.empty())
            newestPeer = peers.begin()->second;
    }

    if (newestPeer != nullptr)
    {
        stats.webrtcConnectionState = stateName(newestPeer->state.load(std::memory_order_acquire));
        stats.webrtcIceConnectionState = iceStateName(newestPeer->iceState.load(std::memory_order_acquire));
    }
    else
    {
        stats.webrtcConnectionState = "closed";
        stats.webrtcIceConnectionState = "closed";
    }
}

void WebRtcAudioSender::sendJson(mg_connection* connection, const juce::String& json) const
{
    if (connection == nullptr || json.isEmpty())
        return;

    mg_websocket_write(connection,
                       MG_WEBSOCKET_OPCODE_TEXT,
                       json.toRawUTF8(),
                       json.getNumBytesAsUTF8());
}

juce::String WebRtcAudioSender::stateName(int state)
{
    switch (static_cast<rtc::PeerConnection::State> (state))
    {
        case rtc::PeerConnection::State::New: return "new";
        case rtc::PeerConnection::State::Connecting: return "connecting";
        case rtc::PeerConnection::State::Connected: return "connected";
        case rtc::PeerConnection::State::Disconnected: return "disconnected";
        case rtc::PeerConnection::State::Failed: return "failed";
        case rtc::PeerConnection::State::Closed: return "closed";
    }

    return "unknown";
}

juce::String WebRtcAudioSender::iceStateName(int state)
{
    switch (static_cast<rtc::PeerConnection::IceState> (state))
    {
        case rtc::PeerConnection::IceState::New: return "new";
        case rtc::PeerConnection::IceState::Checking: return "checking";
        case rtc::PeerConnection::IceState::Connected: return "connected";
        case rtc::PeerConnection::IceState::Completed: return "completed";
        case rtc::PeerConnection::IceState::Failed: return "failed";
        case rtc::PeerConnection::IceState::Disconnected: return "disconnected";
        case rtc::PeerConnection::IceState::Closed: return "closed";
    }

    return "unknown";
}
}
