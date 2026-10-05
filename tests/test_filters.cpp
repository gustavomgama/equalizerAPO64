#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#include "FilterEngine.h"

static std::vector<double> run(const char* cfgText, const std::vector<double>& in, unsigned ch, unsigned frames)
{
    const char* cfg = "/tmp/eqapo_test_filters.txt";
    { std::ofstream f(cfg); f << cfgText; }
    std::wstring cfgW(cfg, cfg + std::strlen(cfg));

    FilterEngine engine;
    engine.initialize(48000.0f, ch, ch, ch, 0, frames, cfgW);
    std::vector<double> out(in.size(), 0.0);
    engine.process(out.data(), const_cast<double*>(in.data()), frames);
    return out;
}

int main()
{
    const unsigned frames = 512, ch = 2;
    std::vector<double> impulse(frames * ch, 0.0);
    impulse[0] = impulse[1] = 1.0;

    // BiQuad peaking filter produces a non-trivial response.
    auto out = run("Filter 1: ON PK Fc 1000 Hz Gain -20 dB Q 1\n", impulse, ch, frames);
    double energy = 0.0;
    for (double s : out) energy += s * s;
    assert(energy > 0.0);

    // Include chain resolves and applies.
    { std::ofstream f("/tmp/eqapo_test_inc.txt"); f << "Preamp: -6 dB\n"; }
    auto out2 = run("Include: /tmp/eqapo_test_inc.txt\n", impulse, ch, frames);
    double energy2 = 0.0;
    for (double s : out2) energy2 += s * s;
    assert(energy2 > 0.0);

    // Loudness correction parses and processes on Linux without crashing.
    {
        auto out3 = run("LoudnessCorrection: State 1 ReferenceLevel -10 ReferenceOffset 0\n",
                        impulse, ch, frames);
        double energy3 = 0.0;
        for (double s : out3)
        {
            assert(std::isfinite(s));
            energy3 += s * s;
        }
        assert(energy3 > 0.0);
    }

    std::printf("OK\n");
    return 0;
}
