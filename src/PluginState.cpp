#include "PluginState.h"

namespace pgstream
{
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { ParamIDs::streamEnabled, 1 }, "Enable Stream", false));

    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID { ParamIDs::httpsPort, 1 }, "Port", 1024, 65535, 8123));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { ParamIDs::transportMode, 1 }, "Stream Engine",
        juce::StringArray {
            "WebRTC Opus",
            "PCM16 DataChannel",
            "PCM24 DataChannel",
            "PCM32F DataChannel",
            "Auto"
        },
        0));

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
        juce::StringArray { "Ultra Low", "Low", "Medium", "Safe", "Very Safe" }, 2));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { ParamIDs::keepAlive, 1 }, "Keep Alive When Idle", true));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { ParamIDs::useSelfSignedCertificate, 1 }, "Use Self-Signed HTTPS Certificate", true));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { ParamIDs::audioPassthrough, 1 }, "Audio Passthrough", true));

    return { params.begin(), params.end() };
}

StreamConfig configFromState(const juce::AudioProcessorValueTreeState& state, double sessionSampleRate)
{
    StreamConfig config;
    config.sessionSampleRate = sessionSampleRate > 1.0 ? sessionSampleRate : 48000.0;

    config.streamEnabled = state.getRawParameterValue(ParamIDs::streamEnabled)->load() >= 0.5f;
    config.port = juce::jlimit(1024, 65535, static_cast<int> (state.getRawParameterValue(ParamIDs::httpsPort)->load()));
    config.keepAliveWhenIdle = state.getRawParameterValue(ParamIDs::keepAlive)->load() >= 0.5f;
    config.useSelfSignedCertificate = state.getRawParameterValue(ParamIDs::useSelfSignedCertificate)->load() >= 0.5f;
    config.audioPassthrough = state.getRawParameterValue(ParamIDs::audioPassthrough)->load() >= 0.5f;

    const auto transportIndex = static_cast<int> (state.getRawParameterValue(ParamIDs::transportMode)->load());
    if (transportIndex == 1)
        config.selectedTransportMode = TransportMode::pcm16DataChannel;
    else if (transportIndex == 2)
        config.selectedTransportMode = TransportMode::pcm24DataChannel;
    else if (transportIndex == 3)
        config.selectedTransportMode = TransportMode::pcm32FloatDataChannel;
    else if (transportIndex == 4)
        config.selectedTransportMode = TransportMode::autoSelect;
    else
        config.selectedTransportMode = TransportMode::opusWebRtc;
    config.transportMode = StreamConfig::resolveTransportMode(config.selectedTransportMode, config.useSelfSignedCertificate);

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
        config.latencyMode = LatencyMode::ultraLowExperimental;
    else if (latencyIndex == 1)
        config.latencyMode = LatencyMode::lowLatency;
    else if (latencyIndex == 3)
        config.latencyMode = LatencyMode::safe;
    else if (latencyIndex == 4)
        config.latencyMode = LatencyMode::verySafe;
    else
        config.latencyMode = LatencyMode::medium;

    return config;
}
}
