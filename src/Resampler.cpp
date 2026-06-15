#include "Resampler.h"
#include <algorithm>
#include <cmath>

namespace pgstream
{
void Resampler::reset()
{
    history.clear();
    phase = 0.0;
    previousSourceRate = 0;
    previousTargetRate = 0;
}

void Resampler::process(const float* inputInterleavedStereo,
                        size_t inputFrames,
                        int sourceRate,
                        int targetRate,
                        std::vector<float>& outputInterleavedStereo)
{
    outputInterleavedStereo.clear();

    if (inputInterleavedStereo == nullptr || inputFrames == 0 || sourceRate <= 0 || targetRate <= 0)
        return;

    if (sourceRate == targetRate)
    {
        outputInterleavedStereo.assign(inputInterleavedStereo, inputInterleavedStereo + inputFrames * 2);
        history.clear();
        phase = 0.0;
        previousSourceRate = sourceRate;
        previousTargetRate = targetRate;
        return;
    }

    if (sourceRate != previousSourceRate || targetRate != previousTargetRate)
        reset();

    previousSourceRate = sourceRate;
    previousTargetRate = targetRate;

    std::vector<float> source;
    source.reserve(history.size() + inputFrames * 2);
    source.insert(source.end(), history.begin(), history.end());
    source.insert(source.end(), inputInterleavedStereo, inputInterleavedStereo + inputFrames * 2);

    const auto sourceFrames = source.size() / 2;
    const auto step = static_cast<double> (sourceRate) / static_cast<double> (targetRate);

    while (phase + 1.0 < static_cast<double> (sourceFrames))
    {
        const auto base = static_cast<size_t> (phase);
        const auto frac = static_cast<float> (phase - static_cast<double> (base));
        const auto i0 = base * 2;
        const auto i1 = i0 + 2;

        const auto left = source[i0] + (source[i1] - source[i0]) * frac;
        const auto right = source[i0 + 1] + (source[i1 + 1] - source[i0 + 1]) * frac;
        outputInterleavedStereo.push_back(left);
        outputInterleavedStereo.push_back(right);

        phase += step;
    }

    const auto consumedFrames = phase >= 1.0 ? static_cast<size_t> (phase) : size_t { 0 };
    if (consumedFrames > 0)
        phase -= static_cast<double> (consumedFrames);

    const auto keepStart = consumedFrames * 2;
    if (keepStart < source.size())
        history.assign(source.begin() + static_cast<std::ptrdiff_t> (keepStart), source.end());
    else
        history.clear();

    if (history.size() > 8)
        history.erase(history.begin(), history.end() - 8);
}
}

