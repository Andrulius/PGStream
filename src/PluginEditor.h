#pragma once

#include "PluginProcessor.h"
#include <juce_gui_extra/juce_gui_extra.h>
#include <functional>

namespace pgstream
{
class CircularTextButton final : public juce::Button
{
public:
    explicit CircularTextButton(const juce::String& textToDraw);
    void paintButton(juce::Graphics&, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

private:
    juce::String text;
};

class AboutPanel final : public juce::Component
{
public:
    explicit AboutPanel(juce::Image popupLogo);

    std::function<void()> onClose;
    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    juce::Image logo;
    CircularTextButton closeButton { "X" };
};

class SettingsPanel final : public juce::Component
{
public:
    SettingsPanel();

    std::function<void()> onClose;
    juce::ToggleButton httpsButton { "Use self-signed certificate" };

    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    juce::Label titleLabel;
    juce::Label helpLabel;
    CircularTextButton closeButton { "X" };
};

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
    void commitPortText();
    void syncPortText(int port);
    void updateEngineAndBitrateUi(const StreamStats& stats);
    juce::String pcmBitrateTextFor(const juce::String& transportName) const;

    PGStreamAudioProcessor& processor;

    juce::ToggleButton enableStreamButton { "Enable Stream" };
    CircularTextButton infoButton { "i" };
    CircularTextButton settingsButton { "S" };
    juce::TextEditor portEditor;
    juce::ComboBox transportBox;
    juce::ComboBox bitrateBox;
    juce::Label bitrateReadoutLabel;
    juce::ComboBox latencyBox;
    juce::ToggleButton keepAliveButton { "Keep stream alive when idle" };
    juce::ToggleButton audioPassthroughButton { "Audio passthrough" };
    juce::ToggleButton alwaysOnTopButton { "Always on top" };
    juce::ToggleButton nerdButton { "Nerd" };

    juce::Label portLabel;
    juce::Label portHintLabel;
    juce::Label transportLabel;
    juce::Label bitrateLabel;
    juce::Label latencyLabel;
    juce::Label engineHintLabel;
    juce::Label urlLabel;
    juce::Label clientsLabel;
    juce::Label statusLabel;
    juce::Label countersLabel;
    juce::Label candidateUrlsLabel;
    juce::TextEditor qrWarningText;
    juce::Image logoImage;
    juce::ImageComponent qrCodeImage;
    juce::Label qrCodeLabel;
    juce::String qrCodeUrl;
    AboutPanel aboutPanel;
    SettingsPanel settingsPanel;
    bool updatingPortText = false;

    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    std::unique_ptr<ButtonAttachment> enableAttachment;
    std::unique_ptr<ComboBoxAttachment> transportAttachment;
    std::unique_ptr<ComboBoxAttachment> bitrateAttachment;
    std::unique_ptr<ComboBoxAttachment> latencyAttachment;
    std::unique_ptr<ButtonAttachment> keepAliveAttachment;
    std::unique_ptr<ButtonAttachment> httpsAttachment;
    std::unique_ptr<ButtonAttachment> audioPassthroughAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PGStreamAudioProcessorEditor)
};
}
