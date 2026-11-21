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
                                       .25, 1., 1., 1., 1., true, false, true);
        const int phase = i % length;
        if (i < length || phase >= fade) {
            const float expected = phase / static_cast<float>(length * 4);
            if (out != anks::Stereo{expected, -expected})
                return false;
        }
    }
    const anks::Stereo input{.293f, -.13f};
    return engine.process(input, 1., 120., true, .25, 1., 2., 2., 1., true, true, true) == input;
}

bool verifyRepeatAutomation(double rate) {
    anks::Engine engine;
    engine.prepare(rate);
    const int captureEnd = static_cast<int>(std::lround(rate / 8.));
    const int trigger = static_cast<int>(rate * .18);
    const int checkOffset = static_cast<int>(rate * .095);
    const int release = static_cast<int>(rate * .30);
    const int dryCheck = release + checkOffset;

    for (int i = 0; i <= dryCheck; ++i) {
        const float sample = i / static_cast<float>(rate);
        const anks::Stereo dry{sample, -sample};
        const bool repeat = i >= trigger && i < release;
        const auto out = engine.process(dry, i * 2. / rate, 120., true,
                                        .25, 1., 1., 1., 1., true, false, repeat);
        if ((i == captureEnd + 20 || i == dryCheck) && out != dry)
            return false;
        if (i == trigger + checkOffset) {
            const float expected = checkOffset / static_cast<float>(rate);
            if (out != anks::Stereo{expected, -expected})
                return false;
        }
    }
    return true;
}

bool verifyPitch() {
    constexpr double rate = 48000.;
    constexpr int slice = 6000;
    constexpr int cycle = 4 * slice;
    constexpr int offset = 480;
    constexpr anks::Pitch pattern{anks::PitchSequence::Pattern, 0, {-12, 12, -12, 0}};
    constexpr anks::Pitch down{anks::PitchSequence::Step, -12, {}};
    for (const auto& pitch : {pattern, down}) {
        anks::Engine engine;
        engine.prepare(rate);
        for (int i = 0; i < 2 * cycle; ++i) {
            const auto source = static_cast<float>(i % cycle) / cycle;
            const auto output = engine.process({source, -source}, i * 2. / rate,
                                                120., true, .25, 1., 1., 1., 1.,
                                                true, false, true, pitch);
            const int phase = i % slice;
            if (phase != offset)
                continue;
            const int note = i % cycle / slice;
            constexpr std::array patternRates{1., 2., .5, 1.};
            constexpr std::array stepRates{1., .5, .25, .25};
            const auto& rates = pitch.mode == anks::PitchSequence::Pattern
                ? patternRates : stepRates;
            const auto expected = static_cast<float>(offset * rates[note] / cycle);
            if (std::abs(output[0] - expected) > 1.e-6f ||
                std::abs(output[1] + expected) > 1.e-6f)
                return false;
        }
    }

    anks::Engine engine;
    engine.prepare(rate);
    constexpr int trigger = 8400;
    constexpr int afterRamp = 4800;
    for (int i = 0; i <= trigger + afterRamp; ++i) {
        const auto source = static_cast<float>(i) / cycle;
        const auto output = engine.process({source, -source}, i * 2. / rate,
                                            120., true, .25, 1., 1., 1., 1.,
                                            true, false, i >= trigger, pattern);
        if (i == trigger + afterRamp) {
            const auto expected = static_cast<float>(afterRamp / 2) / cycle;
            if (std::abs(output[0] - expected) > 1.e-6f)
                return false;
        }
    }
    return true;
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
                                           t * 2, 120., true, .25, 1., 1., 1., 1., true, !wet, true);
        for (const float channel : sample)
            littleEndian(out, static_cast<std::uint16_t>(std::lround(channel * 32767)), 2);
    }
    return static_cast<bool>(out);
}
}

int main(int argc, char** argv) {
    if (!verifyPitch()) {
        std::cerr << "Pitch timing or sequence mismatch\n";
        return 1;
    }
    std::cout << "Pitch pattern, step, Refresh and Repeat reset OK\n";
    for (const double rate : {44100., 48000., 96000.})
        if (!verify(rate) || !verifyRepeatAutomation(rate)) {
            std::cerr << "Capture, bypass or Repeat automation mismatch at " << rate << " Hz\n";
            return 1;
        }
    std::cout << "Capture, bypass and Repeat automation OK at 44.1 / 48 / 96 kHz\n";
    if (argc == 3 && !(render(argv[1], false) && render(argv[2], true)))
        return 2;
}
