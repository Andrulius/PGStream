#pragma once

#include <juce_core/juce_core.h>

namespace pgstream
{
struct NetworkInterfaceSelection
{
    juce::String primaryAddress;
    juce::String primaryUrl;
    juce::StringArray candidateUrls;
    juce::StringArray candidateDescriptions;
};

NetworkInterfaceSelection selectBestLanInterface(int port);
}
