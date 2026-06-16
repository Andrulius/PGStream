#pragma once

#include "StreamTypes.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace pgstream
{
namespace ParamIDs
{
static constexpr const char* streamEnabled = "streamEnabled";
static constexpr const char* httpsPort = "httpsPort";
static constexpr const char* outputFormat = "outputFormat";
static constexpr const char* sampleRateMode = "outputSampleRateMode";
static constexpr const char* packetDuration = "packetDurationMode";
static constexpr const char* bufferTarget = "bufferTargetMs";
static constexpr const char* keepAlive = "keepAliveWhenIdle";
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
juce::StringArray bufferTargetChoiceLabels();
int bufferTargetMsForIndex(int index);
StreamConfig configFromState(const juce::AudioProcessorValueTreeState& state, double sessionSampleRate);
}
