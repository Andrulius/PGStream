#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <cstdint>
#include <vector>

namespace pgstream
{
class AudioTapFifo
{
public:
    void prepare(size_t requestedFrames);
    void reset();

    void pushFromAudioThread(const juce::AudioBuffer<float>& buffer, int inputChannels, int frameCount) noexcept;
    size_t pop(float* destinationInterleavedStereo, size_t maxFrames) noexcept;

    uint64_t getDroppedFrames() const noexcept { return droppedFrames.load(std::memory_order_relaxed); }

private:
    size_t capacityFrames = 0;
    size_t capacityMask = 0;
    std::vector<float> samples;
    std::atomic<uint64_t> readIndex { 0 };
    std::atomic<uint64_t> writeIndex { 0 };
    std::atomic<uint64_t> droppedFrames { 0 };
};
}

