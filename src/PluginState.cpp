#include "PluginState.h"

namespace pgstream
{
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { ParamIDs::streamEnabled, 1 }, "Enable Stream", false));

    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID { ParamIDs::httpsPort, 1 }, "HTTPS Port", 1024, 65535, 8123));

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
        juce::StringArray { "100", "250", "500", "1000", "20", "40", "60" }, 0));

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
    static constexpr int targets[] = { 100, 250, 500, 1000, 20, 40, 60 };
    config.bufferTargetMs = targets[juce::jlimit(0, 6, bufferIndex)];

    return config;
}
}
