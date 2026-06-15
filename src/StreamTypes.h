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

struct StreamConfig
{
    bool streamEnabled = false;
    int port = 8123;
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
};

struct StreamStats
{
    bool serverRunning = false;
    bool streamEnabled = false;
    int port = 8123;
    int connectedClients = 0;
    juce::String lanUrl;
    juce::String currentLanIp;
    juce::String listenAddress;
    juce::String statusText;
    juce::String streamFormat;
    int streamSampleRate = 48000;
    int bufferTargetMs = 100;
    int packetDurationMs = 20;
    juce::String packetMode;
    uint64_t framesSent = 0;
    uint64_t serverFifoUnderruns = 0;
    uint64_t networkPacketsSent = 0;
    uint64_t websocketSendFailures = 0;
    uint64_t fifoDroppedFrames = 0;
    juce::StringArray candidateLanUrls;
    juce::StringArray candidateLanDescriptions;
};

constexpr uint32_t protocolVersion = 1;
constexpr size_t frameHeaderBytes = 28;
constexpr uint32_t frameFlagSilence = 1u;
}
