#pragma once

#include <juce_core/juce_core.h>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace pgstream
{
enum class OutputFormat : uint16_t
{
    float32 = 1,
    pcm16 = 2
};

enum class SampleRateMode : int
{
    session = 0,
    hz48000 = 1,
    hz44100 = 2
};

enum class PacketDurationMode : int
{
    ms20 = 0,
    ms40 = 1,
    ms60 = 2,
    extr666me = 3
};

enum class TransportMode : int
{
    webrtcOpus = 0,
    websocketLegacy = 1
};

enum class OpusBitrateMode : int
{
    goodPreview128 = 0,
    veryGood192 = 1,
    studioPreview256 = 2,
    highQuality320 = 3,
    experimentalMax510 = 4
};

enum class LatencyMode : int
{
    safe = 0,
    balanced = 1,
    lowLatency = 2,
    ultraLowExperimental = 3
};

struct StreamConfig
{
    bool streamEnabled = false;
    int port = 8123;
    TransportMode transportMode = TransportMode::webrtcOpus;
    OpusBitrateMode opusBitrateMode = OpusBitrateMode::highQuality320;
    LatencyMode latencyMode = LatencyMode::balanced;
    OutputFormat outputFormat = OutputFormat::float32;
    SampleRateMode sampleRateMode = SampleRateMode::session;
    PacketDurationMode packetDurationMode = PacketDurationMode::ms20;
    int bufferTargetMs = 100;
    bool keepAliveWhenIdle = true;
    double sessionSampleRate = 48000.0;

    int targetSampleRate() const
    {
        if (sampleRateMode == SampleRateMode::hz48000)
            return 48000;
        if (sampleRateMode == SampleRateMode::hz44100)
            return 44100;
        return juce::jlimit(1, 384000, static_cast<int> (std::round(sessionSampleRate)));
    }

    juce::String formatName() const
    {
        return outputFormat == OutputFormat::float32 ? "Float32" : "PCM16";
    }

    juce::String sampleRateModeName() const
    {
        if (sampleRateMode == SampleRateMode::hz48000)
            return "48000";
        if (sampleRateMode == SampleRateMode::hz44100)
            return "44100";
        return "Session";
    }

    int packetDurationMs() const
    {
        if (packetDurationMode == PacketDurationMode::ms40)
            return 40;
        if (packetDurationMode == PacketDurationMode::ms60)
            return 60;
        if (packetDurationMode == PacketDurationMode::extr666me)
            return 5;
        return 20;
    }

    int packetFrameCount() const
    {
        const auto frames = static_cast<int> (std::round(static_cast<double> (targetSampleRate()) * packetDurationMs() / 1000.0));
        return juce::jmax(1, frames);
    }

    juce::String packetModeName() const
    {
        if (packetDurationMode == PacketDurationMode::ms40)
            return "40 ms";
        if (packetDurationMode == PacketDurationMode::ms60)
            return "60 ms";
        if (packetDurationMode == PacketDurationMode::extr666me)
            return "extr666me (5 ms experimental)";
        return "20 ms";
    }

    juce::String transportModeName() const
    {
        return transportMode == TransportMode::websocketLegacy ? "WebSocket Legacy - Fallback"
                                                               : "WebRTC Opus - Recommended";
    }

    int opusBitrateBps() const
    {
        if (opusBitrateMode == OpusBitrateMode::goodPreview128)
            return 128000;
        if (opusBitrateMode == OpusBitrateMode::veryGood192)
            return 192000;
        if (opusBitrateMode == OpusBitrateMode::studioPreview256)
            return 256000;
        if (opusBitrateMode == OpusBitrateMode::experimentalMax510)
            return 510000;
        return 320000;
    }

    juce::String opusBitrateName() const
    {
        if (opusBitrateMode == OpusBitrateMode::goodPreview128)
            return "128 kb/s Good Preview";
        if (opusBitrateMode == OpusBitrateMode::veryGood192)
            return "192 kb/s Very Good";
        if (opusBitrateMode == OpusBitrateMode::studioPreview256)
            return "256 kb/s Studio Preview";
        if (opusBitrateMode == OpusBitrateMode::experimentalMax510)
            return "510 kb/s Experimental Max";
        return "320 kb/s High Quality / Recommended";
    }

    juce::String latencyModeName() const
    {
        if (latencyMode == LatencyMode::safe)
            return "Safe";
        if (latencyMode == LatencyMode::lowLatency)
            return "Low Latency";
        if (latencyMode == LatencyMode::ultraLowExperimental)
            return "Ultra Low / Experimental";
        return "Balanced";
    }

    juce::String latencyTargetDescription() const
    {
        if (latencyMode == LatencyMode::safe)
            return "about 80-150 ms";
        if (latencyMode == LatencyMode::lowLatency)
            return "about 25-60 ms";
        if (latencyMode == LatencyMode::ultraLowExperimental)
            return "about 15-40 ms experimental";
        return "about 40-90 ms";
    }

    int opusFrameDurationMs() const
    {
        if (latencyMode == LatencyMode::lowLatency)
            return 10;
        if (latencyMode == LatencyMode::ultraLowExperimental)
            return 5;
        return 20;
    }
};

struct StreamStats
{
    bool serverRunning = false;
    bool streamEnabled = false;
    int port = 8123;
    int connectedClients = 0;
    int webrtcPeerCount = 0;
    int webrtcOpenTracks = 0;
    juce::String lanUrl;
    juce::String currentLanIp;
    juce::String listenAddress;
    juce::String statusText;
    juce::String transportMode;
    juce::String streamFormat;
    int streamSampleRate = 48000;
    int bufferTargetMs = 100;
    int packetDurationMs = 20;
    juce::String packetMode;
    int opusBitrateBps = 320000;
    juce::String opusBitratePreset;
    juce::String opusCodec { "opus/48000/2" };
    bool opusBitrateLimited = false;
    juce::String latencyMode;
    juce::String latencyTarget;
    int opusFrameDurationMs = 20;
    uint64_t framesSent = 0;
    uint64_t serverFifoUnderruns = 0;
    uint64_t networkPacketsSent = 0;
    uint64_t websocketSendFailures = 0;
    uint64_t fifoDroppedFrames = 0;
    size_t senderQueueFillFrames = 0;
    size_t senderQueueCapacityFrames = 0;
    double senderQueueFillMs = 0.0;
    uint64_t webrtcEncodedFrames = 0;
    uint64_t webrtcEncodedPackets = 0;
    uint64_t webrtcEncodedBytes = 0;
    uint64_t webrtcSendCalls = 0;
    uint64_t webrtcEncoderOverloadWarnings = 0;
    juce::String webrtcConnectionState;
    juce::String webrtcIceConnectionState;
    juce::StringArray candidateLanUrls;
    juce::StringArray candidateLanDescriptions;
};

constexpr uint32_t protocolVersion = 1;
constexpr size_t frameHeaderBytes = 28;
constexpr uint32_t frameFlagSilence = 1u;
}
