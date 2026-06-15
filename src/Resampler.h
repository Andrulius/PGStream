#pragma once

#include <vector>

namespace pgstream
{
class Resampler
{
public:
    void reset();
    void process(const float* inputInterleavedStereo,
                 size_t inputFrames,
                 int sourceRate,
                 int targetRate,
                 std::vector<float>& outputInterleavedStereo);

private:
    std::vector<float> history;
    double phase = 0.0;
    int previousSourceRate = 0;
    int previousTargetRate = 0;
};
}

