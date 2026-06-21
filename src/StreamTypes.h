#pragma once

#include <juce_core/juce_core.h>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace pgstream
{
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
    OpusBitrateMode opusBitrateMode = OpusBitrateMode::highQuality320;
    LatencyMode latencyMode = LatencyMode::balanced;
    bool keepAliveWhenIdle = true;
    double sessionSampleRate = 48000.0;

    juce::String transportModeName() const
    {
        return "WebRTC Opus";
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

    int playoutDelayHintMs() const
    {
        if (latencyMode == LatencyMode::safe)
            return 150;
        if (latencyMode == LatencyMode::lowLatency || latencyMode == LatencyMode::ultraLowExperimental)
            return 30;
        return 80;
    }

    int opusFrameDurationMs() const
    {
        if (latencyMode == LatencyMode::lowLatency)
            return 10;
        if (latencyMode == LatencyMode::ultraLowExperimental)
            return 10;
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
    int inputSampleRate = 48000;
    int selectedOpusBitrateBps = 320000;
    juce::String selectedOpusBitratePreset;
    int opusBitrateBps = 320000;
    juce::String opusBitratePreset;
    juce::String opusCodec { "opus/48000/2" };
    bool opusBitrateLimited = false;
    juce::String selectedLatencyMode;
    juce::String latencyMode;
    juce::String latencyTarget;
    int opusFrameDurationMs = 20;
    int activePlayoutDelayHintMs = 80;
    bool autonegotiationEnabled = false;
    juce::String autonegotiationMode;
    juce::String autonegotiationState;
    juce::String lastAdaptationReason;
    juce::String lastAdaptationTimestamp;
    uint64_t stateRevision = 0;
    juce::String stateOrigin;
    juce::String lastUserRequestedBitratePreset;
    juce::String lastUserRequestedLatencyMode;
    juce::String lastStateUpdateSentTimestamp;
    uint64_t framesSent = 0;
    uint64_t serverFifoUnderruns = 0;
    uint64_t fifoDroppedFrames = 0;
    size_t senderQueueFillFrames = 0;
    size_t senderQueueCapacityFrames = 0;
    double senderQueueFillMs = 0.0;
    uint64_t webrtcEncodedFrames = 0;
    uint64_t webrtcEncodedPackets = 0;
    uint64_t webrtcEncodedBytes = 0;
    uint64_t webrtcSendCalls = 0;
    uint64_t webrtcRtpPacketsAttempted = 0;
    uint64_t webrtcRtpPacketsSent = 0;
    uint64_t webrtcRtpSendFailures = 0;
    uint64_t webrtcEncodeOverBudgetCount = 0;
    uint64_t webrtcEncoderOverloadWarnings = 0;
    int webrtcNegotiatedPayloadType = 111;
    int webrtcActualPayloadType = 111;
    uint32_t webrtcSsrc = 0;
    uint16_t webrtcSequenceCurrent = 0;
    uint32_t webrtcTimestampCurrent = 0;
    uint32_t webrtcTimestampIncrementExpected = 960;
    uint32_t webrtcTimestampIncrementActual = 0;
    uint64_t webrtcTimestampAnomalyCount = 0;
    uint64_t webrtcPacketsSubmittedToTrack = 0;
    uint64_t webrtcBytesSubmittedToTrack = 0;
    uint64_t webrtcSubmitErrors = 0;
    uint64_t opusEncodeErrors = 0;
    int opusPacketBytesLast = 0;
    double opusPacketBytesAvg = 0.0;
    double inputRmsL = 0.0;
    double inputRmsR = 0.0;
    juce::String webrtcConnectionState;
    juce::String webrtcIceConnectionState;
    juce::StringArray candidateLanUrls;
    juce::StringArray candidateLanDescriptions;
};
}
