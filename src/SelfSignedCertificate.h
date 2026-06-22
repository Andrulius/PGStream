#pragma once

#include <juce_core/juce_core.h>

namespace pgstream
{
struct CertificateFiles
{
    bool ready = false;
    juce::File pemFile;
    juce::String description;
};

CertificateFiles ensureSelfSignedCertificate(const juce::String& lanIpAddress);
}
