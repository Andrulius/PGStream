#pragma once

#include "PluginProcessor.h"
#include <juce_gui_extra/juce_gui_extra.h>

namespace pgstream
{
class PGStreamAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                           private juce::Timer
{
public:
    explicit PGStreamAudioProcessorEditor(PGStreamAudioProcessor&);
    ~PGStreamAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void addLabeled(juce::Label& label, juce::Component& component, const juce::String& text);

    PGStreamAudioProcessor& processor;

    juce::ToggleButton enableStreamButton { "Enable Stream" };
    juce::Slider portSlider;
    juce::ComboBox formatBox;
    juce::ComboBox sampleRateBox;
    juce::ComboBox packetBox;
    juce::ComboBox bufferBox;
    juce::ToggleButton keepAliveButton { "Keep stream alive when idle" };
    juce::ToggleButton nerdButton { "Nerd" };

    juce::Label portLabel;
    juce::Label formatLabel;
    juce::Label sampleRateLabel;
    juce::Label packetLabel;
    juce::Label bufferLabel;
    juce::Label urlLabel;
    juce::Label clientsLabel;
    juce::Label statusLabel;
    juce::Label countersLabel;
    juce::Label candidateUrlsLabel;
    juce::Label certNoteLabel;
    juce::Image logoImage;
    juce::ImageComponent qrCodeImage;
    juce::Label qrCodeLabel;
    juce::String qrCodeUrl;

    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    std::unique_ptr<ButtonAttachment> enableAttachment;
    std::unique_ptr<SliderAttachment> portAttachment;
    std::unique_ptr<ComboBoxAttachment> formatAttachment;
    std::unique_ptr<ComboBoxAttachment> sampleRateAttachment;
    std::unique_ptr<ComboBoxAttachment> packetAttachment;
    std::unique_ptr<ComboBoxAttachment> bufferAttachment;
    std::unique_ptr<ButtonAttachment> keepAliveAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PGStreamAudioProcessorEditor)
};
}
