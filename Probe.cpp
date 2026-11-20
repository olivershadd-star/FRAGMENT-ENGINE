#include "Engine.h"
#include <cstdint>
#include <fstream>
#include <iostream>
#include <numbers>

namespace {
bool verify(double rate) {
    anks::Engine engine;
    engine.prepare(rate);
    const auto length = static_cast<int>(std::lround(rate / 8.));
    const auto fade = static_cast<int>(rate * .002);
    for (int i = 0; i < static_cast<int>(rate / 2.); ++i) {
        const float sample = i / static_cast<float>(length * 4);
        const auto out = engine.process({sample, -sample}, i * 2. / rate, 120., true,
                                       .25, 1., 1., 1., 1., true, false);
        const int phase = i % length;
        if (i < length || phase >= fade) {
            const float expected = phase / static_cast<float>(length * 4);
            if (out != anks::Stereo{expected, -expected})
                return false;
        }
    }
    const anks::Stereo input{.293f, -.13f};
    return engine.process(input, 1., 120., true, .25, 1., 2., 2., 1., true, true) == input;
}

void littleEndian(std::ostream& out, std::uint32_t value, int bytes) {
    for (int i = 0; i < bytes; ++i) {
        out.put(static_cast<char>(value & 255));
        value >>= 8;
    }
}

bool render(const char* path, bool wet) {
    constexpr int rate = 48000;
    constexpr int frames = rate * 8;
    anks::Engine engine;
    engine.prepare(rate);
    std::ofstream out(path, std::ios::binary);
    out << "RIFF";
    littleEndian(out, 36 + frames * 4, 4);
    out << "WAVEfmt ";
    littleEndian(out, 16, 4);
    littleEndian(out, 1, 2);
    littleEndian(out, 2, 2);
    littleEndian(out, rate, 4);
    littleEndian(out, rate * 4, 4);
    littleEndian(out, 4, 2);
    littleEndian(out, 16, 2);
    out << "data";
    littleEndian(out, frames * 4, 4);
    for (int i = 0; i < frames; ++i) {
        const double t = i / static_cast<double>(rate);
        const double pulse = std::fmod(t, .5);
        const double offbeat = std::fmod(t + .25, .5);
        const double kick = .57 * std::exp(-26 * pulse) *
            std::sin(2 * std::numbers::pi * (49 * pulse + 2.5 * (1 - std::exp(-44 * pulse))));
        const double tick = .24 * std::exp(-85 * offbeat) * std::sin(14613 * offbeat);
        const double tone = .13 * std::exp(-9 * pulse) *
            std::sin(2 * std::numbers::pi * (110 + 27.5 * (i / (rate / 2) % 4)) * pulse);
        const auto sample = engine.process({static_cast<float>(kick + tick + tone),
                                           static_cast<float>(kick - tick + tone * .7)},
                                           t * 2, 120., true, .25, 1., 1., 1., 1., true, !wet);
        for (const float channel : sample)
            littleEndian(out, static_cast<std::uint16_t>(std::lround(channel * 32767)), 2);
    }
    return static_cast<bool>(out);
}
}

int main(int argc, char** argv) {
    for (const double rate : {44100., 48000., 96000.})
        if (!verify(rate)) {
            std::cerr << "Capture/bypass mismatch at " << rate << " Hz\n";
            return 1;
        }
    std::cout << "Capture and bypass OK at 44.1 / 48 / 96 kHz\n";
    if (argc == 3 && !(render(argv[1], false) && render(argv[2], true)))
        return 2;
}
