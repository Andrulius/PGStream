#include "PluginState.h"
#include <array>

namespace pgstream
{
namespace
{
constexpr std::array<int, 7> bufferTargetValuesMs { 20, 40, 60, 100, 250, 500, 1000 };
}

juce::StringArray bufferTargetChoiceLabels()
{
    juce::StringArray labels;
    for (const auto targetMs : bufferTargetValuesMs)
        labels.add(juce::String(targetMs) + " ms");

    return labels;
}

int bufferTargetMsForIndex(int index)
{
    const auto safeIndex = juce::jlimit(0, static_cast<int> (bufferTargetValuesMs.size()) - 1, index);
    return bufferTargetValuesMs[static_cast<size_t> (safeIndex)];
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { ParamIDs::streamEnabled, 1 }, "Enable Stream", false));

    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID { ParamIDs::httpsPort, 1 }, "HTTPS Port", 1024, 65535, 8123));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { ParamIDs::transportMode, 1 }, "Transport",
        juce::StringArray { "WebRTC Opus - Recommended", "WebSocket Legacy - Fallback" }, 0));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { ParamIDs::opusBitrate, 1 }, "Opus Bitrate",
        juce::StringArray {
            "128 kb/s Good Preview",
            "192 kb/s Very Good",
            "256 kb/s Studio Preview",
            "320 kb/s High Quality / Recommended",
            "510 kb/s Experimental Max"
        },
        3));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { ParamIDs::latencyMode, 1 }, "Latency Mode",
        juce::StringArray { "Safe", "Balanced", "Low Latency", "Ultra Low / Experimental" }, 1));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { ParamIDs::outputFormat, 1 }, "Format",
        juce::StringArray { "Float32", "PCM16" }, 0));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { ParamIDs::sampleRateMode, 1 }, "Sample Rate",
        juce::StringArray { "Session", "48000", "44100" }, 0));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { ParamIDs::packetDuration, 1 }, "Packet Duration",
        juce::StringArray { "20 ms", "40 ms", "60 ms", "extr666me" }, 0));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { ParamIDs::bufferTarget, 1 }, "Buffer Target",
        bufferTargetChoiceLabels(), 3));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { ParamIDs::keepAlive, 1 }, "Keep Alive When Idle", true));

    return { params.begin(), params.end() };
}

StreamConfig configFromState(const juce::AudioProcessorValueTreeState& state, double sessionSampleRate)
{
    StreamConfig config;
    config.sessionSampleRate = sessionSampleRate > 1.0 ? sessionSampleRate : 48000.0;

    config.streamEnabled = state.getRawParameterValue(ParamIDs::streamEnabled)->load() >= 0.5f;
    config.port = juce::jlimit(1024, 65535, static_cast<int> (state.getRawParameterValue(ParamIDs::httpsPort)->load()));
    config.keepAliveWhenIdle = state.getRawParameterValue(ParamIDs::keepAlive)->load() >= 0.5f;

    const auto transportIndex = static_cast<int> (state.getRawParameterValue(ParamIDs::transportMode)->load());
    config.transportMode = transportIndex == 1 ? TransportMode::websocketLegacy : TransportMode::webrtcOpus;

    const auto bitrateIndex = static_cast<int> (state.getRawParameterValue(ParamIDs::opusBitrate)->load());
    if (bitrateIndex == 0)
        config.opusBitrateMode = OpusBitrateMode::goodPreview128;
    else if (bitrateIndex == 1)
        config.opusBitrateMode = OpusBitrateMode::veryGood192;
    else if (bitrateIndex == 2)
        config.opusBitrateMode = OpusBitrateMode::studioPreview256;
    else if (bitrateIndex == 4)
        config.opusBitrateMode = OpusBitrateMode::experimentalMax510;
    else
        config.opusBitrateMode = OpusBitrateMode::highQuality320;

    const auto latencyIndex = static_cast<int> (state.getRawParameterValue(ParamIDs::latencyMode)->load());
    if (latencyIndex == 0)
        config.latencyMode = LatencyMode::safe;
    else if (latencyIndex == 2)
        config.latencyMode = LatencyMode::lowLatency;
    else if (latencyIndex == 3)
        config.latencyMode = LatencyMode::ultraLowExperimental;
    else
        config.latencyMode = LatencyMode::balanced;

    const auto formatIndex = static_cast<int> (state.getRawParameterValue(ParamIDs::outputFormat)->load());
    config.outputFormat = formatIndex == 1 ? OutputFormat::pcm16 : OutputFormat::float32;

    const auto sampleRateIndex = static_cast<int> (state.getRawParameterValue(ParamIDs::sampleRateMode)->load());
    if (sampleRateIndex == 1)
        config.sampleRateMode = SampleRateMode::hz48000;
    else if (sampleRateIndex == 2)
        config.sampleRateMode = SampleRateMode::hz44100;
    else
        config.sampleRateMode = SampleRateMode::session;

    const auto packetIndex = static_cast<int> (state.getRawParameterValue(ParamIDs::packetDuration)->load());
    if (packetIndex == 1)
        config.packetDurationMode = PacketDurationMode::ms40;
    else if (packetIndex == 2)
        config.packetDurationMode = PacketDurationMode::ms60;
    else if (packetIndex == 3)
        config.packetDurationMode = PacketDurationMode::extr666me;
    else
        config.packetDurationMode = PacketDurationMode::ms20;

    const auto bufferIndex = static_cast<int> (state.getRawParameterValue(ParamIDs::bufferTarget)->load());
    config.bufferTargetMs = bufferTargetMsForIndex(bufferIndex);

    return config;
}
}
