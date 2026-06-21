#pragma once

#include "StreamTypes.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace pgstream
{
namespace ParamIDs
{
static constexpr const char* streamEnabled = "streamEnabled";
static constexpr const char* httpsPort = "httpsPort";
static constexpr const char* opusBitrate = "opusBitrate";
static constexpr const char* latencyMode = "latencyMode";
static constexpr const char* keepAlive = "keepAliveWhenIdle";
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
StreamConfig configFromState(const juce::AudioProcessorValueTreeState& state, double sessionSampleRate);
}
