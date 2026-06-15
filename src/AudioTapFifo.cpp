#include "AudioTapFifo.h"

namespace pgstream
{
static size_t nextPowerOfTwo(size_t value)
{
    size_t result = 1;
    while (result < value)
        result <<= 1;
    return result;
}

void AudioTapFifo::prepare(size_t requestedFrames)
{
    capacityFrames = nextPowerOfTwo(std::max<size_t> (requestedFrames, 4096));
    capacityMask = capacityFrames - 1;
    samples.assign(capacityFrames * 2, 0.0f);
    reset();
}

void AudioTapFifo::reset()
{
    readIndex.store(0, std::memory_order_release);
    writeIndex.store(0, std::memory_order_release);
    droppedFrames.store(0, std::memory_order_release);
}

void AudioTapFifo::pushFromAudioThread(const juce::AudioBuffer<float>& buffer, int inputChannels, int frameCount) noexcept
{
    if (capacityFrames == 0 || frameCount <= 0 || inputChannels <= 0)
        return;

    auto framesToWrite = static_cast<size_t> (frameCount);
    auto sourceOffset = size_t { 0 };

    if (framesToWrite > capacityFrames)
    {
        sourceOffset = framesToWrite - capacityFrames;
        droppedFrames.fetch_add(sourceOffset, std::memory_order_relaxed);
        framesToWrite = capacityFrames;
    }

    auto write = writeIndex.load(std::memory_order_relaxed);
    auto read = readIndex.load(std::memory_order_acquire);
    auto used = static_cast<size_t> (write - read);
    auto freeFrames = capacityFrames - used;

    while (freeFrames < framesToWrite)
    {
        const auto needToDrop = framesToWrite - freeFrames;
        const auto desiredRead = read + std::min<size_t> (needToDrop, used);
        if (readIndex.compare_exchange_weak(read, desiredRead, std::memory_order_acq_rel))
        {
            droppedFrames.fetch_add(desiredRead - read, std::memory_order_relaxed);
            read = desiredRead;
        }

        used = static_cast<size_t> (write - read);
        freeFrames = capacityFrames - used;
    }

    const auto* left = buffer.getReadPointer(0);
    const auto* right = inputChannels > 1 ? buffer.getReadPointer(1) : left;

    for (size_t i = 0; i < framesToWrite; ++i)
    {
        const auto source = sourceOffset + i;
        const auto destination = static_cast<size_t> ((write + i) & capacityMask) * 2;
        samples[destination] = left[source];
        samples[destination + 1] = right[source];
    }

    writeIndex.store(write + framesToWrite, std::memory_order_release);
}

size_t AudioTapFifo::pop(float* destinationInterleavedStereo, size_t maxFrames) noexcept
{
    if (capacityFrames == 0 || maxFrames == 0 || destinationInterleavedStereo == nullptr)
        return 0;

    auto read = readIndex.load(std::memory_order_relaxed);
    const auto write = writeIndex.load(std::memory_order_acquire);
    const auto available = std::min<size_t> (maxFrames, static_cast<size_t> (write - read));

    for (size_t i = 0; i < available; ++i)
    {
        const auto source = static_cast<size_t> ((read + i) & capacityMask) * 2;
        destinationInterleavedStereo[i * 2] = samples[source];
        destinationInterleavedStereo[i * 2 + 1] = samples[source + 1];
    }

    readIndex.store(read + available, std::memory_order_release);
    return available;
}
}

