#include "PluginEditor.h"
#include <BinaryData.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <utility>
#include <vector>

namespace pgstream
{
namespace
{
constexpr int qrVersion = 3;
constexpr int qrSize = 17 + qrVersion * 4;
constexpr int qrDataCodewords = 55;
constexpr int qrEcCodewords = 15;
constexpr int qrTotalCodewords = qrDataCodewords + qrEcCodewords;

int gfMultiply(int x, int y)
{
    int result = 0;
    while (y != 0)
    {
        if ((y & 1) != 0)
            result ^= x;

        x <<= 1;
        if ((x & 0x100) != 0)
            x ^= 0x11d;

        y >>= 1;
    }

    return result;
}

std::array<uint8_t, qrEcCodewords> makeReedSolomonDivisor()
{
    std::array<uint8_t, qrEcCodewords> result {};
    result[qrEcCodewords - 1] = 1;

    int root = 1;
    for (int i = 0; i < qrEcCodewords; ++i)
    {
        for (int j = 0; j < qrEcCodewords; ++j)
        {
            result[static_cast<size_t> (j)] = static_cast<uint8_t> (gfMultiply(result[static_cast<size_t> (j)], root));
            if (j + 1 < qrEcCodewords)
                result[static_cast<size_t> (j)] ^= result[static_cast<size_t> (j + 1)];
        }

        root = gfMultiply(root, 0x02);
    }

    return result;
}

std::array<uint8_t, qrEcCodewords> makeErrorCorrection(const std::array<uint8_t, qrDataCodewords>& data)
{
    const auto divisor = makeReedSolomonDivisor();
    std::array<uint8_t, qrEcCodewords> result {};

    for (const auto byte : data)
    {
        const auto factor = static_cast<uint8_t> (byte ^ result[0]);

        for (int i = 0; i + 1 < qrEcCodewords; ++i)
            result[static_cast<size_t> (i)] = result[static_cast<size_t> (i + 1)];
        result[qrEcCodewords - 1] = 0;

        for (int i = 0; i < qrEcCodewords; ++i)
            result[static_cast<size_t> (i)] ^= static_cast<uint8_t> (gfMultiply(divisor[static_cast<size_t> (i)], factor));
    }

    return result;
}

void appendBits(std::vector<bool>& bits, uint32_t value, int count)
{
    for (int i = count - 1; i >= 0; --i)
        bits.push_back(((value >> i) & 1u) != 0);
}

bool getBit(uint32_t value, int index)
{
    return ((value >> index) & 1u) != 0;
}

uint32_t makeFormatBits(int mask)
{
    uint32_t data = static_cast<uint32_t> ((1 << 3) | mask); // QR error correction level L.
    uint32_t bits = data << 10;
    for (int i = 14; i >= 10; --i)
    {
        if (((bits >> i) & 1u) != 0)
            bits ^= 0x537u << (i - 10);
    }

    return ((data << 10) | bits) ^ 0x5412u;
}

class QrMatrix
{
public:
    QrMatrix()
        : modules(static_cast<size_t> (qrSize * qrSize), false),
          functionModules(static_cast<size_t> (qrSize * qrSize), false)
    {
    }

    void setFunction(int x, int y, bool value)
    {
        if (! isInBounds(x, y))
            return;

        const auto index = toIndex(x, y);
        modules[index] = value;
        functionModules[index] = true;
    }

    void setData(int x, int y, bool value)
    {
        modules[toIndex(x, y)] = value;
    }

    bool isFunction(int x, int y) const
    {
        return functionModules[toIndex(x, y)];
    }

    bool get(int x, int y) const
    {
        return modules[toIndex(x, y)];
    }

private:
    static bool isInBounds(int x, int y)
    {
        return x >= 0 && y >= 0 && x < qrSize && y < qrSize;
    }

    static size_t toIndex(int x, int y)
    {
        return static_cast<size_t> (y * qrSize + x);
    }

    std::vector<bool> modules;
    std::vector<bool> functionModules;
};

void drawFinder(QrMatrix& qr, int x, int y)
{
    for (int dy = -1; dy <= 7; ++dy)
    {
        for (int dx = -1; dx <= 7; ++dx)
        {
            const auto xx = x + dx;
            const auto yy = y + dy;
            const auto black = dx >= 0 && dx <= 6 && dy >= 0 && dy <= 6
                && (dx == 0 || dx == 6 || dy == 0 || dy == 6 || (dx >= 2 && dx <= 4 && dy >= 2 && dy <= 4));

            qr.setFunction(xx, yy, black);
        }
    }
}

void drawAlignment(QrMatrix& qr, int centerX, int centerY)
{
    for (int dy = -2; dy <= 2; ++dy)
    {
        for (int dx = -2; dx <= 2; ++dx)
        {
            const auto distance = juce::jmax(std::abs(dx), std::abs(dy));
            qr.setFunction(centerX + dx, centerY + dy, distance != 1);
        }
    }
}

void drawFunctionPatterns(QrMatrix& qr)
{
    drawFinder(qr, 0, 0);
    drawFinder(qr, qrSize - 7, 0);
    drawFinder(qr, 0, qrSize - 7);
    drawAlignment(qr, 22, 22);

    for (int i = 0; i < qrSize; ++i)
    {
        if (! qr.isFunction(6, i))
            qr.setFunction(6, i, i % 2 == 0);
        if (! qr.isFunction(i, 6))
            qr.setFunction(i, 6, i % 2 == 0);
    }

    const auto formatBits = makeFormatBits(0);
    for (int i = 0; i <= 5; ++i)
        qr.setFunction(8, i, getBit(formatBits, i));
    qr.setFunction(8, 7, getBit(formatBits, 6));
    qr.setFunction(8, 8, getBit(formatBits, 7));
    qr.setFunction(7, 8, getBit(formatBits, 8));
    for (int i = 9; i < 15; ++i)
        qr.setFunction(14 - i, 8, getBit(formatBits, i));

    for (int i = 0; i < 8; ++i)
        qr.setFunction(qrSize - 1 - i, 8, getBit(formatBits, i));
    for (int i = 8; i < 15; ++i)
        qr.setFunction(8, qrSize - 15 + i, getBit(formatBits, i));

    qr.setFunction(8, qrSize - 8, true);
}

std::array<uint8_t, qrDataCodewords> encodeQrData(const juce::String& text)
{
    const auto utf8 = text.toRawUTF8();
    const auto byteCount = static_cast<size_t> (std::strlen(utf8));
    std::vector<bool> bits;
    bits.reserve(qrDataCodewords * 8);

    appendBits(bits, 0x4, 4);
    appendBits(bits, static_cast<uint32_t> (byteCount), 8);
    for (size_t i = 0; i < byteCount; ++i)
        appendBits(bits, static_cast<uint8_t> (utf8[i]), 8);

    const auto capacityBits = static_cast<size_t> (qrDataCodewords * 8);
    const auto terminator = juce::jmin<size_t> (4, capacityBits - bits.size());
    for (size_t i = 0; i < terminator; ++i)
        bits.push_back(false);

    while ((bits.size() % 8) != 0)
        bits.push_back(false);

    std::array<uint8_t, qrDataCodewords> data {};
    size_t byteIndex = 0;
    for (; byteIndex < bits.size() / 8 && byteIndex < data.size(); ++byteIndex)
    {
        uint8_t value = 0;
        for (int bit = 0; bit < 8; ++bit)
            value = static_cast<uint8_t> ((value << 1) | (bits[byteIndex * 8 + static_cast<size_t> (bit)] ? 1 : 0));
        data[byteIndex] = value;
    }

    uint8_t pad = 0xec;
    while (byteIndex < data.size())
    {
        data[byteIndex++] = pad;
        pad = pad == 0xec ? 0x11 : 0xec;
    }

    return data;
}

std::array<uint8_t, qrTotalCodewords> makeQrCodewords(const juce::String& text)
{
    const auto data = encodeQrData(text);
    const auto ec = makeErrorCorrection(data);

    std::array<uint8_t, qrTotalCodewords> codewords {};
    std::copy(data.begin(), data.end(), codewords.begin());
    std::copy(ec.begin(), ec.end(), codewords.begin() + qrDataCodewords);
    return codewords;
}

bool mask0(int x, int y)
{
    return ((x + y) & 1) == 0;
}

juce::String transportValueName(int index)
{
    if (index == 1)
        return "PCM16 DataChannel";
    if (index == 2)
        return "PCM24 DataChannel";
    if (index == 3)
        return "PCM32F DataChannel";
    if (index == 4)
        return "Auto";
    return "WebRTC Opus";
}

juce::Image makeQrImage(const juce::String& text)
{
    if (text.isEmpty() || text.getNumBytesAsUTF8() > 53)
        return {};

    auto qr = QrMatrix();
    drawFunctionPatterns(qr);

    const auto codewords = makeQrCodewords(text);
    int bitIndex = 0;

    for (int right = qrSize - 1; right >= 1; right -= 2)
    {
        if (right == 6)
            --right;

        for (int vert = 0; vert < qrSize; ++vert)
        {
            const auto upward = ((right + 1) & 2) == 0;
            const auto y = upward ? qrSize - 1 - vert : vert;

            for (int j = 0; j < 2; ++j)
            {
                const auto x = right - j;
                if (qr.isFunction(x, y))
                    continue;

                bool bit = false;
                if (bitIndex < qrTotalCodewords * 8)
                    bit = getBit(codewords[static_cast<size_t> (bitIndex >> 3)], 7 - (bitIndex & 7));

                qr.setData(x, y, bit ^ mask0(x, y));
                ++bitIndex;
            }
        }
    }

    constexpr int quietZone = 4;
    constexpr int pixelsPerModule = 5;
    constexpr int imageSize = (qrSize + quietZone * 2) * pixelsPerModule;
    juce::Image image(juce::Image::RGB, imageSize, imageSize, true);
    juce::Graphics g(image);
    g.fillAll(juce::Colours::white);
    g.setColour(juce::Colours::black);

    for (int y = 0; y < qrSize; ++y)
    {
        for (int x = 0; x < qrSize; ++x)
        {
            if (qr.get(x, y))
            {
                g.fillRect((x + quietZone) * pixelsPerModule,
                           (y + quietZone) * pixelsPerModule,
                           pixelsPerModule,
                           pixelsPerModule);
            }
        }
    }

    return image;
}
}

CircularTextButton::CircularTextButton(const juce::String& textToDraw)
    : juce::Button(textToDraw), text(textToDraw)
{
    setWantsKeyboardFocus(false);
}

void CircularTextButton::paintButton(juce::Graphics& g, bool highlighted, bool down)
{
    const auto bounds = getLocalBounds().toFloat().reduced(1.0f);
    auto fill = juce::Colour(0xff252b30);
    if (highlighted)
        fill = fill.brighter(0.12f);
    if (down)
        fill = fill.darker(0.12f);

    g.setColour(fill);
    g.fillEllipse(bounds);
    g.setColour(juce::Colour(0xff8fd3ff));
    g.drawEllipse(bounds, 1.4f);
    g.setColour(juce::Colour(0xfff3f5f7));
    g.setFont(juce::Font(text == "i" ? 15.0f : 13.0f, juce::Font::bold));
    g.drawText(text, getLocalBounds(), juce::Justification::centred, false);
}

AboutPanel::AboutPanel(juce::Image popupLogo)
    : logo(std::move(popupLogo))
{
    setWantsKeyboardFocus(true);
    addAndMakeVisible(closeButton);
    closeButton.onClick = [this]
    {
        if (onClose)
            onClose();
    };
}

void AboutPanel::paint(juce::Graphics& g)
{
    g.setColour(juce::Colour(0xcc000000));
    g.fillAll();

    const auto panel = getLocalBounds().reduced(34, 24).toFloat();
    g.setColour(juce::Colour(0xff20252a));
    g.fillRoundedRectangle(panel, 8.0f);
    g.setColour(juce::Colour(0xff46535d));
    g.drawRoundedRectangle(panel, 8.0f, 1.0f);

    auto content = getLocalBounds().reduced(54, 42);
    content.removeFromTop(6);

    if (logo.isValid())
    {
        const auto logoArea = content.removeFromTop(142);
        g.drawImageWithin(logo,
                          logoArea.getCentreX() - 62,
                          logoArea.getY(),
                          124,
                          124,
                          juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize);
        content.removeFromTop(4);
    }

    g.setColour(juce::Colour(0xfff3f5f7));
    g.setFont(juce::Font(22.0f, juce::Font::bold));
    g.drawText("Pigeon Stream", content.removeFromTop(30), juce::Justification::centred);

    g.setFont(juce::Font(14.0f));
    g.setColour(juce::Colour(0xffd8e0e6));

    const juce::String copyright = juce::String::charToString(0x00a9) + " 2026 Aras Pigeon";
    const juce::String description = "Transparent VST3 for streaming audio from the DAW master bus, "
        "and other DAW buses if inserted there, to a browser over LAN.";
    const juce::String text = juce::String("Version 0.8\n"
        "Author: Aras Pigeon\n\n"
    ) + description + "\n\n"
        + copyright + "\n"
        "https://github.com/Andrulius/PGStream\n"
        "License: AGPL-3.0-only";

    g.drawFittedText(text, content.reduced(8, 0), juce::Justification::centredTop, 9);
}

void AboutPanel::resized()
{
    closeButton.setBounds(getWidth() - 68, 38, 26, 26);
}

bool AboutPanel::keyPressed(const juce::KeyPress& key)
{
    if (key.getKeyCode() == juce::KeyPress::escapeKey)
    {
        if (onClose)
            onClose();
        return true;
    }

    return false;
}

SettingsPanel::SettingsPanel()
{
    setWantsKeyboardFocus(true);

    titleLabel.setText("Settings", juce::dontSendNotification);
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    titleLabel.setColour(juce::Label::textColourId, juce::Colour(0xfff3f5f7));
    titleLabel.setFont(juce::Font(18.0f, juce::Font::bold));
    addAndMakeVisible(titleLabel);

    helpLabel.setText("Turning this off serves the receiver over HTTP/WS and forces Opus.\nLossless PCM requires self-signed HTTPS.", juce::dontSendNotification);
    helpLabel.setJustificationType(juce::Justification::topLeft);
    helpLabel.setColour(juce::Label::textColourId, juce::Colour(0xffc7d0d8));
    addAndMakeVisible(helpLabel);

    addAndMakeVisible(httpsButton);
    addAndMakeVisible(closeButton);

    closeButton.onClick = [this]
    {
        if (onClose)
            onClose();
    };
}

void SettingsPanel::paint(juce::Graphics& g)
{
    g.setColour(juce::Colour(0xcc000000));
    g.fillAll();

    const auto panel = getLocalBounds().reduced(58, 72).toFloat();
    g.setColour(juce::Colour(0xff20252a));
    g.fillRoundedRectangle(panel, 8.0f);
    g.setColour(juce::Colour(0xff46535d));
    g.drawRoundedRectangle(panel, 8.0f, 1.0f);
}

void SettingsPanel::resized()
{
    auto panel = getLocalBounds().reduced(76, 92);
    closeButton.setBounds(panel.getRight() - 30, panel.getY(), 26, 26);
    titleLabel.setBounds(panel.removeFromTop(30).removeFromLeft(160));
    panel.removeFromTop(12);
    httpsButton.setBounds(panel.removeFromTop(28));
    panel.removeFromTop(10);
    helpLabel.setBounds(panel.removeFromTop(72));
}

bool SettingsPanel::keyPressed(const juce::KeyPress& key)
{
    if (key.getKeyCode() == juce::KeyPress::escapeKey)
    {
        if (onClose)
            onClose();
        return true;
    }

    return false;
}

PGStreamAudioProcessorEditor::PGStreamAudioProcessorEditor(PGStreamAudioProcessor& owner)
    : juce::AudioProcessorEditor(&owner),
      processor(owner),
      aboutPanel(juce::ImageFileFormat::loadFrom(PGStreamBinaryData::logo_png,
                                                 static_cast<size_t> (PGStreamBinaryData::logo_pngSize)))
{
    setSize(600, 560);
    logoImage = juce::ImageFileFormat::loadFrom(PGStreamBinaryData::pgs_png,
                                                static_cast<size_t> (PGStreamBinaryData::pgs_pngSize));

    portEditor.setInputRestrictions(5, "0123456789");
    portEditor.setJustification(juce::Justification::centredLeft);
    portEditor.setSelectAllWhenFocused(true);
    portEditor.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff20262a));
    portEditor.setColour(juce::TextEditor::textColourId, juce::Colour(0xfff3f5f7));
    portEditor.setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff46535d));
    portEditor.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colour(0xff8fd3ff));
    portEditor.onReturnKey = [this] { commitPortText(); };
    portEditor.onFocusLost = [this] { commitPortText(); };

    transportBox.addItem("WebRTC Opus", 1);
    transportBox.addItem("PCM16 DataChannel", 2);
    transportBox.addItem("PCM24 DataChannel", 3);
    transportBox.addItem("PCM32F DataChannel", 4);
    transportBox.addItem("Auto", 5);

    bitrateBox.addItem("128 kb/s Good Preview", 1);
    bitrateBox.addItem("192 kb/s Very Good", 2);
    bitrateBox.addItem("256 kb/s Studio Preview", 3);
    bitrateBox.addItem("320 kb/s High Quality / Recommended", 4);
    bitrateBox.addItem("510 kb/s Experimental Max", 5);

    bitrateReadoutLabel.setJustificationType(juce::Justification::centredLeft);
    bitrateReadoutLabel.setColour(juce::Label::backgroundColourId, juce::Colour(0xff20262a));
    bitrateReadoutLabel.setColour(juce::Label::outlineColourId, juce::Colour(0xff46535d));
    bitrateReadoutLabel.setColour(juce::Label::textColourId, juce::Colour(0xfff3f5f7));

    latencyBox.addItem("Ultra Low", 1);
    latencyBox.addItem("Low", 2);
    latencyBox.addItem("Medium", 3);
    latencyBox.addItem("Safe", 4);
    latencyBox.addItem("Very Safe", 5);

    addAndMakeVisible(enableStreamButton);
    addAndMakeVisible(infoButton);
    infoButton.onClick = [this]
    {
        aboutPanel.setVisible(true);
        aboutPanel.toFront(true);
        aboutPanel.grabKeyboardFocus();
    };
    addAndMakeVisible(settingsButton);
    settingsButton.onClick = [this]
    {
        settingsPanel.setVisible(true);
        settingsPanel.toFront(true);
        settingsPanel.grabKeyboardFocus();
    };

    addLabeled(portLabel, portEditor, "Port");
    addAndMakeVisible(portHintLabel);
    portHintLabel.setText("Valid range: 1024-65535", juce::dontSendNotification);
    portHintLabel.setJustificationType(juce::Justification::centredLeft);
    portHintLabel.setColour(juce::Label::textColourId, juce::Colour(0xfff0d7a7));
    portHintLabel.setVisible(false);
    addLabeled(transportLabel, transportBox, "Engine");
    addLabeled(bitrateLabel, bitrateBox, "Opus Bitrate");
    addAndMakeVisible(bitrateReadoutLabel);
    addLabeled(latencyLabel, latencyBox, "Latency Mode");
    addAndMakeVisible(keepAliveButton);
    addAndMakeVisible(audioPassthroughButton);
    addAndMakeVisible(alwaysOnTopButton);
    alwaysOnTopButton.onClick = [this]
    {
        if (auto* topLevel = getTopLevelComponent())
            topLevel->setAlwaysOnTop(alwaysOnTopButton.getToggleState());
    };
    addAndMakeVisible(nerdButton);
    nerdButton.onClick = [this]
    {
        setSize(getWidth(), nerdButton.getToggleState() ? 760 : 560);
        resized();
        timerCallback();
    };

    addAndMakeVisible(qrCodeImage);
    qrCodeImage.setImagePlacement(juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize);

    addAndMakeVisible(qrCodeLabel);
    qrCodeLabel.setJustificationType(juce::Justification::centred);
    qrCodeLabel.setColour(juce::Label::textColourId, juce::Colour(0xffc7d0d8));
    qrCodeLabel.setText("QR appears when streaming is enabled.", juce::dontSendNotification);

    addAndMakeVisible(qrWarningText);
    qrWarningText.setReadOnly(true);
    qrWarningText.setMultiLine(true);
    qrWarningText.setScrollbarsShown(false);
    qrWarningText.setCaretVisible(false);
    qrWarningText.setText("Local self-signed HTTPS certificate. Your browser may show a security warning. Check that the IP address matches this computer. Continue only if you trust this device and your LAN.", false);
    qrWarningText.setColour(juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    qrWarningText.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    qrWarningText.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    qrWarningText.setColour(juce::TextEditor::textColourId, juce::Colour(0xfff0d7a7));
    qrWarningText.setFont(juce::Font(11.0f));

    addAndMakeVisible(engineHintLabel);
    engineHintLabel.setText("Lossless PCM requires self-signed HTTPS. Enable it in Settings to use PCM16/PCM24/PCM32F.", juce::dontSendNotification);
    engineHintLabel.setJustificationType(juce::Justification::centredLeft);
    engineHintLabel.setColour(juce::Label::textColourId, juce::Colour(0xfff0d7a7));
    engineHintLabel.setVisible(false);

    for (auto* label : { &urlLabel, &clientsLabel, &statusLabel, &countersLabel, &candidateUrlsLabel })
    {
        addAndMakeVisible(*label);
        label->setJustificationType(juce::Justification::centredLeft);
        label->setColour(juce::Label::textColourId, juce::Colours::white);
    }

    countersLabel.setColour(juce::Label::textColourId, juce::Colour(0xffd8e0e6));
    candidateUrlsLabel.setColour(juce::Label::textColourId, juce::Colour(0xffc7d0d8));

    addChildComponent(aboutPanel);
    aboutPanel.onClose = [this]
    {
        aboutPanel.setVisible(false);
        infoButton.grabKeyboardFocus();
    };
    addChildComponent(settingsPanel);
    settingsPanel.onClose = [this]
    {
        settingsPanel.setVisible(false);
        settingsButton.grabKeyboardFocus();
    };

    enableAttachment = std::make_unique<ButtonAttachment>(processor.parameters, ParamIDs::streamEnabled, enableStreamButton);
    transportAttachment = std::make_unique<ComboBoxAttachment>(processor.parameters, ParamIDs::transportMode, transportBox);
    bitrateAttachment = std::make_unique<ComboBoxAttachment>(processor.parameters, ParamIDs::opusBitrate, bitrateBox);
    latencyAttachment = std::make_unique<ComboBoxAttachment>(processor.parameters, ParamIDs::latencyMode, latencyBox);
    keepAliveAttachment = std::make_unique<ButtonAttachment>(processor.parameters, ParamIDs::keepAlive, keepAliveButton);
    httpsAttachment = std::make_unique<ButtonAttachment>(processor.parameters, ParamIDs::useSelfSignedCertificate, settingsPanel.httpsButton);
    audioPassthroughAttachment = std::make_unique<ButtonAttachment>(processor.parameters, ParamIDs::audioPassthrough, audioPassthroughButton);

    syncPortText(8123);
    startTimerHz(4);
    timerCallback();
}

PGStreamAudioProcessorEditor::~PGStreamAudioProcessorEditor()
{
    stopTimer();
}

void PGStreamAudioProcessorEditor::addLabeled(juce::Label& label, juce::Component& component, const juce::String& text)
{
    addAndMakeVisible(label);
    addAndMakeVisible(component);
    label.setText(text, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centredLeft);
    label.setColour(juce::Label::textColourId, juce::Colour(0xffc7d0d8));
}

void PGStreamAudioProcessorEditor::commitPortText()
{
    if (updatingPortText)
        return;

    auto value = portEditor.getText().getIntValue();
    const auto valid = value >= 1024 && value <= 65535;
    if (! valid)
    {
        const auto stats = processor.getStreamStats();
        value = juce::jlimit(1024, 65535, stats.port);
    }

    syncPortText(value);

    if (auto* parameter = processor.parameters.getParameter(ParamIDs::httpsPort))
    {
        const auto current = static_cast<int> (processor.parameters.getRawParameterValue(ParamIDs::httpsPort)->load());
        if (current != value)
            parameter->setValueNotifyingHost(parameter->convertTo0to1(static_cast<float> (value)));
    }
}

void PGStreamAudioProcessorEditor::syncPortText(int port)
{
    juce::ScopedValueSetter<bool> setter(updatingPortText, true);
    portEditor.setText(juce::String(juce::jlimit(1024, 65535, port)), juce::dontSendNotification);
}

juce::String PGStreamAudioProcessorEditor::pcmBitrateTextFor(const juce::String& transportName) const
{
    const auto lower = transportName.toLowerCase();
    if (lower.contains("pcm16"))
        return "PCM16 48 kHz stereo, approx. 1536 kb/s";
    if (lower.contains("pcm24"))
        return "PCM24 48 kHz stereo, approx. 2304 kb/s";
    if (lower.contains("pcm32"))
        return "PCM32F 48 kHz stereo, approx. 3072 kb/s";
    return {};
}

void PGStreamAudioProcessorEditor::updateEngineAndBitrateUi(const StreamStats& stats)
{
    const auto selected = stats.selectedTransportMode.isNotEmpty()
        ? stats.selectedTransportMode
        : transportValueName(transportBox.getSelectedItemIndex());
    const auto pcmText = pcmBitrateTextFor(selected == "Auto" ? stats.transportMode : selected);
    const auto httpsEnabled = stats.httpsEnabled;
    const auto pcmSelectedOrActive = pcmText.isNotEmpty();

    transportBox.setEnabled(httpsEnabled);
    engineHintLabel.setVisible(! httpsEnabled);
    bitrateBox.setVisible(! pcmSelectedOrActive);
    bitrateReadoutLabel.setVisible(pcmSelectedOrActive);
    bitrateReadoutLabel.setText(pcmSelectedOrActive ? pcmText : juce::String(), juce::dontSendNotification);
    bitrateLabel.setText(pcmSelectedOrActive ? "Bitrate" : "Opus Bitrate", juce::dontSendNotification);
}

void PGStreamAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff181b1e));
    g.setColour(juce::Colour(0xfff3f5f7));
    g.setFont(juce::Font(24.0f, juce::Font::bold));
    g.drawText("Pigeon Stream", 18, 14, 210, 32, juce::Justification::centredLeft);

    g.setFont(juce::Font(13.0f));
    g.setColour(juce::Colour(0xffc7d0d8));
    g.drawText("PGStream.vst3", 20, 48, 190, 22, juce::Justification::centredLeft);

    if (logoImage.isValid())
    {
        g.drawImageWithin(logoImage,
                          getWidth() - 160,
                          12,
                          142,
                          142,
                          juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize);
    }
}

void PGStreamAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced(18);
    const auto topArea = area.removeFromTop(206);
    const auto logoSize = 142;
    const auto qrSizePx = 136;
    const auto logoX = topArea.getRight() - logoSize;
    const auto qrX = logoX - 30 - qrSizePx;
    infoButton.setBounds(topArea.getX() + 2, topArea.getY() + 64, 24, 24);
    settingsButton.setBounds(infoButton.getRight() + 8, infoButton.getY(), 24, 24);
    keepAliveButton.setBounds(topArea.getX(), topArea.getY() + 96, 220, 24);
    audioPassthroughButton.setBounds(topArea.getX(), topArea.getY() + 122, 220, 24);
    alwaysOnTopButton.setBounds(topArea.getX(), topArea.getY() + 148, 220, 24);
    qrCodeImage.setBounds(qrX, topArea.getY(), qrSizePx, qrSizePx);
    qrCodeLabel.setBounds(qrX, qrCodeImage.getBottom() + 4, qrSizePx, 24);
    qrWarningText.setBounds(qrX - 8, qrCodeLabel.getBottom() + 2, qrSizePx + 16, 48);

    area.removeFromTop(2);

    enableStreamButton.setBounds(area.removeFromTop(30));
    area.removeFromTop(8);

    auto row = area.removeFromTop(34);
    portLabel.setBounds(row.removeFromLeft(120));
    portEditor.setBounds(row.removeFromLeft(150));
    row.removeFromLeft(10);
    portHintLabel.setBounds(row);

    area.removeFromTop(8);
    row = area.removeFromTop(32);
    transportLabel.setBounds(row.removeFromLeft(120));
    transportBox.setBounds(row.removeFromLeft(250));
    row.removeFromLeft(10);
    engineHintLabel.setBounds(row);

    area.removeFromTop(8);
    row = area.removeFromTop(32);
    bitrateLabel.setBounds(row.removeFromLeft(120));
    bitrateBox.setBounds(row.removeFromLeft(240));
    bitrateReadoutLabel.setBounds(bitrateBox.getBounds());
    row.removeFromLeft(18);
    latencyLabel.setBounds(row.removeFromLeft(100));
    latencyBox.setBounds(row);

    area.removeFromTop(10);

    urlLabel.setBounds(area.removeFromTop(28));
    statusLabel.setBounds(area.removeFromTop(42));
    clientsLabel.setBounds(area.removeFromTop(22));

    area.removeFromTop(6);
    nerdButton.setBounds(area.removeFromTop(28).removeFromLeft(90));

    const auto nerdVisible = nerdButton.getToggleState();
    countersLabel.setVisible(nerdVisible);
    candidateUrlsLabel.setVisible(nerdVisible);

    area.removeFromTop(6);
    if (nerdVisible)
    {
        countersLabel.setBounds(area.removeFromTop(128));
        candidateUrlsLabel.setBounds(area.removeFromTop(74));
        area.removeFromTop(8);
    }

    aboutPanel.setBounds(getLocalBounds());
    settingsPanel.setBounds(getLocalBounds());
}

void PGStreamAudioProcessorEditor::timerCallback()
{
    const auto stats = processor.getStreamStats();
    const auto url = stats.lanUrl;

    if (! portEditor.hasKeyboardFocus(true))
        syncPortText(stats.port);

    portHintLabel.setVisible(portEditor.hasKeyboardFocus(true));
    updateEngineAndBitrateUi(stats);

    urlLabel.setText("LAN URL: " + (url.isNotEmpty() ? url : "enable stream to bind server"),
                     juce::dontSendNotification);
    clientsLabel.setText("Clients: " + juce::String(stats.webrtcPeerCount)
                            + "    Server: " + (stats.serverRunning ? stats.serverScheme.toUpperCase() : "off")
                            + "    Port: " + juce::String(stats.port),
                         juce::dontSendNotification);
    statusLabel.setText("Active engine: " + stats.transportMode
                            + "    Selected: " + stats.selectedTransportMode
                            + "\nStatus: " + stats.statusText
                            + "    Latency: " + stats.latencyMode
                            + "    WebRTC: " + stats.webrtcConnectionState + "/" + stats.webrtcIceConnectionState,
                        juce::dontSendNotification);

    countersLabel.setText("Active engine: " + stats.transportMode
                              + "    Connection: " + stats.webrtcConnectionState + "/" + stats.webrtcIceConnectionState
                              + "    Clients: " + juce::String(stats.webrtcPeerCount)
                              + "\nBitrate/throughput: " + (stats.transportMode.contains("PCM") ? pcmBitrateTextFor(stats.transportMode) : stats.opusBitratePreset)
                              + "    ACK age: " + juce::String(stats.pcmReceiverAckAgeMs, 0) + " ms"
                              + "    Last reason: " + stats.lastAdaptationReason
                              + "\nFIFO queue: " + juce::String(stats.senderQueueFillMs, 1) + " ms"
                              + "    FIFO underruns/drops: " + juce::String(stats.serverFifoUnderruns)
                              + "/" + juce::String(stats.fifoDroppedFrames)
                              + "    Encode over-budget: " + juce::String(stats.webrtcEncodeOverBudgetCount)
                              + "\nOpus RTP sent/fail: " + juce::String(stats.webrtcRtpPacketsSent)
                              + "/" + juce::String(stats.webrtcRtpSendFailures)
                              + "    Packet loss/jitter: browser Stats"
                              + "\nPCM DC/ready: " + juce::String(stats.pcmOpenChannels)
                              + "/" + juce::String(stats.pcmReceiverReadyCount)
                              + "    DC queued: " + juce::String(static_cast<int64_t> (stats.pcmDataChannelBufferedBytes)) + " bytes"
                              + "    Dropped before send: " + juce::String(stats.pcmDroppedBeforeSend)
                              + "\nPCM stream: " + juce::String(stats.pcmPacketFrames) + " frames/packet"
                              + "    AudioContext: " + (stats.pcmAudioContextSampleRate > 0.0 ? juce::String(stats.pcmAudioContextSampleRate, 0) + " Hz" : "unknown")
                              + "    base/output: " + juce::String(stats.pcmAudioContextBaseLatencyMs, 2)
                              + "/" + juce::String(stats.pcmAudioContextOutputLatencyMs, 2) + " ms"
                              + "\nPCM missing/late/overflow: " + juce::String(stats.pcmReceiverMissingPackets)
                              + "/" + juce::String(stats.pcmReceiverLatePackets)
                              + "/" + juce::String(stats.pcmReceiverOverflows)
                              + "    PCM packets/bytes/fail: " + juce::String(stats.pcmPacketsSent)
                              + "/" + juce::String(stats.pcmBytesSent)
                              + "/" + juce::String(stats.pcmSendFailures)
                              + "\nPCM buffer/target/underruns: " + juce::String(stats.pcmReceiverBufferMs, 1)
                              + "/" + juce::String(stats.pcmTargetBufferMs)
                              + " ms / " + juce::String(stats.pcmReceiverUnderflows)
                              + "    Secure context: browser Stats",
                          juce::dontSendNotification);

    juce::StringArray candidateUrls;
    for (int i = 0; i < stats.candidateLanUrls.size(); ++i)
    {
        const auto candidateUrl = stats.candidateLanUrls[i];
        candidateUrls.add(candidateUrl == stats.lanUrl ? candidateUrl + " (primary)" : candidateUrl);
    }

    const auto candidates = candidateUrls.joinIntoString("\n");
    candidateUrlsLabel.setText("Candidate LAN URLs:\n" + (candidates.isNotEmpty() ? candidates : "none detected"),
                               juce::dontSendNotification);

    if (qrCodeUrl != stats.lanUrl)
    {
        qrCodeUrl = stats.lanUrl;
        const auto qrImage = makeQrImage(qrCodeUrl);
        qrCodeImage.setImage(qrImage);
        qrCodeImage.setVisible(qrImage.isValid());
        qrCodeLabel.setText(qrImage.isValid() ? "Scan on phone" : "QR appears when streaming is enabled.",
                            juce::dontSendNotification);
    }
}
}
