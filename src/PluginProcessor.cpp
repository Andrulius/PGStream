#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace pgstream
{
PGStreamAudioProcessor::PGStreamAudioProcessor()
    : juce::AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "PGStreamState", createParameterLayout()),
      networkServer(tapFifo)
{
    networkServer.setActiveProfileCallback([this] (OpusBitrateMode bitrateMode, LatencyMode latencyMode)
    {
        requestProfileParameterSync(bitrateMode, latencyMode);
    });

    parameters.addParameterListener(ParamIDs::streamEnabled, this);
    parameters.addParameterListener(ParamIDs::httpsPort, this);
    parameters.addParameterListener(ParamIDs::opusBitrate, this);
    parameters.addParameterListener(ParamIDs::latencyMode, this);
    parameters.addParameterListener(ParamIDs::keepAlive, this);
    setLatencySamples(0);
}

PGStreamAudioProcessor::~PGStreamAudioProcessor()
{
    cancelPendingUpdate();
    networkServer.setActiveProfileCallback({});
    parameters.removeParameterListener(ParamIDs::streamEnabled, this);
    parameters.removeParameterListener(ParamIDs::httpsPort, this);
    parameters.removeParameterListener(ParamIDs::opusBitrate, this);
    parameters.removeParameterListener(ParamIDs::latencyMode, this);
    parameters.removeParameterListener(ParamIDs::keepAlive, this);
}

void PGStreamAudioProcessor::prepareToPlay(double sampleRate, int)
{
    currentSampleRate.store(sampleRate, std::memory_order_release);
    networkServer.setSessionSampleRate(sampleRate);

    const auto capacity = static_cast<size_t> (std::max(192000.0, sampleRate * 4.0));
    tapFifo.prepare(capacity);
    triggerAsyncUpdate();
}

void PGStreamAudioProcessor::releaseResources()
{
}

bool PGStreamAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& input = layouts.getMainInputChannelSet();
    const auto& output = layouts.getMainOutputChannelSet();

    if (input.isDisabled() || output.isDisabled())
        return false;

    return input == output && output.size() >= 1;
}

void PGStreamAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    midiMessages.clear();

    const auto totalInputChannels = getTotalNumInputChannels();
    const auto totalOutputChannels = getTotalNumOutputChannels();

    for (auto channel = totalInputChannels; channel < totalOutputChannels; ++channel)
        buffer.clear(channel, 0, buffer.getNumSamples());

    if (tapEnabled.load(std::memory_order_relaxed))
        tapFifo.pushFromAudioThread(buffer, totalInputChannels, buffer.getNumSamples());
}

juce::AudioProcessorEditor* PGStreamAudioProcessor::createEditor()
{
    return new PGStreamAudioProcessorEditor(*this);
}

void PGStreamAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void PGStreamAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState != nullptr && xmlState->hasTagName(parameters.state.getType()))
    {
        parameters.replaceState(juce::ValueTree::fromXml(*xmlState));
        triggerAsyncUpdate();
    }
}

void PGStreamAudioProcessor::parameterChanged(const juce::String&, float)
{
    triggerAsyncUpdate();
}

void PGStreamAudioProcessor::handleAsyncUpdate()
{
    applyPendingProfileParameterSync();
    applyCurrentStreamConfig();
}

void PGStreamAudioProcessor::applyCurrentStreamConfig()
{
    const auto config = configFromState(parameters, currentSampleRate.load(std::memory_order_acquire));
    tapEnabled.store(config.streamEnabled, std::memory_order_release);
    networkServer.applyConfig(config);
}

void PGStreamAudioProcessor::requestProfileParameterSync(OpusBitrateMode bitrateMode, LatencyMode latencyMode)
{
    pendingBitrateMode.store(static_cast<int> (bitrateMode), std::memory_order_release);
    pendingLatencyMode.store(static_cast<int> (latencyMode), std::memory_order_release);
    triggerAsyncUpdate();
}

void PGStreamAudioProcessor::applyPendingProfileParameterSync()
{
    const auto bitrateMode = pendingBitrateMode.exchange(-1, std::memory_order_acq_rel);
    const auto latencyMode = pendingLatencyMode.exchange(-1, std::memory_order_acq_rel);

    if (bitrateMode >= 0)
    {
        if (auto* parameter = parameters.getParameter(ParamIDs::opusBitrate))
        {
            const auto current = static_cast<int> (parameters.getRawParameterValue(ParamIDs::opusBitrate)->load());
            if (current != bitrateMode)
                parameter->setValueNotifyingHost(parameter->convertTo0to1(static_cast<float> (bitrateMode)));
        }
    }

    if (latencyMode >= 0)
    {
        if (auto* parameter = parameters.getParameter(ParamIDs::latencyMode))
        {
            const auto current = static_cast<int> (parameters.getRawParameterValue(ParamIDs::latencyMode)->load());
            if (current != latencyMode)
                parameter->setValueNotifyingHost(parameter->convertTo0to1(static_cast<float> (latencyMode)));
        }
    }
}
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new pgstream::PGStreamAudioProcessor();
}
