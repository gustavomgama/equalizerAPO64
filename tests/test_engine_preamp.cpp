#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#include "FilterEngine.h"

int main()
{
    const char* cfg = "/tmp/eqapo_test_preamp.txt";
    { std::ofstream f(cfg); f << "Preamp: -6 dB\n"; }
    std::wstring cfgW(cfg, cfg + std::strlen(cfg));

    FilterEngine engine;
    const unsigned frames = 512, channels = 2;
    engine.initialize(48000.0f, channels, channels, channels, 0, frames, cfgW);

    std::vector<double> in(frames * channels, 0.5);
    std::vector<double> out(frames * channels, 0.0);
    engine.process(out.data(), in.data(), frames);

    const double expected = 0.5 * std::pow(10.0, -6.0 / 20.0);
    for (double s : out)
        assert(std::fabs(s - expected) < 1e-6);

    std::printf("OK\n");
    return 0;
}
