#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace anks {
using Stereo = std::array<float, 2>;
inline constexpr std::array divisions{4., 2., 1., .5, .25, .125, .0625};
enum Parameter { Enable, InDb, OutDb, Stutter, Refresh, Mix, Bypass, Count };
inline constexpr std::array defaults{1., 2./3, 2./3, 4./6, 2./6, 1., 0.};

inline int divisionIndex(double normalized) {
    return static_cast<int>(std::lround(normalized * (divisions.size() - 1)));
}

class Engine {
public:
    void prepare(double rate) {
        sampleRate = rate;
        grain.resize(static_cast<std::size_t>(std::ceil(rate * 12.)));
        reset();
        primed = false;
    }

    void reset() {
        previousBeat = std::numeric_limits<double>::quiet_NaN();
        captureSize = captured = readPosition = fade = 0;
        lastWet = {};
    }

    Stereo process(Stereo input, double beat, double tempo, bool playing,
                   double stutter, double refresh, double inputGain,
                   double outputGain, double mix, bool enabled, bool bypass) {
        if (!primed || bypass) {
            smoothedIn = inputGain;
            smoothedOut = outputGain;
            smoothedMix = enabled ? mix : 0.;
            primed = true;
        }
        if (bypass) {
            reset();
            return input;
        }
        const double ramp = std::min(1., 200. / sampleRate);
        advance(smoothedIn, inputGain, ramp);
        advance(smoothedOut, outputGain, ramp);
        advance(smoothedMix, enabled && playing ? mix : 0., ramp);
        Stereo dry = input;
        if (smoothedIn != 1.)
            for (auto& sample : dry)
                sample *= static_cast<float>(smoothedIn);

        Stereo wet = dry;
        if (playing && !grain.empty() && (enabled || smoothedMix > 0.)) {
            const double period = std::max(stutter, refresh);
            const double gridCell = std::floor(beat / period + 1.e-9);
            const bool boundary = std::isfinite(previousBeat) && gridCell != std::floor(previousBeat / period + 1.e-9);
            const bool startOnGrid = !std::isfinite(previousBeat)
                && beat - gridCell * period < tempo / (60. * sampleRate);
            if (boundary || startOnGrid) {
                captureSize = std::clamp<std::size_t>(
                    std::lround(stutter * 60. * sampleRate / tempo), 1, grain.size());
                captured = readPosition = 0;
                fadeSize = std::min(captureSize / 8,
                                   static_cast<std::size_t>(sampleRate * .002));
                if (std::isfinite(previousBeat))
                    beginFade();
            }
            previousBeat = beat;
            if (captured < captureSize) {
                grain[captured++] = dry;
            } else if (captureSize > 0) {
                if (readPosition == 0)
                    beginFade();
                wet = grain[readPosition++];
                if (readPosition == captureSize)
                    readPosition = 0;
            }
            if (fade > 0) {
                const float blend = static_cast<float>(fade--) / fadeSize;
                for (std::size_t channel = 0; channel < wet.size(); ++channel)
                    wet[channel] += blend * (fadeFrom[channel] - wet[channel]);
            }
            lastWet = wet;
        } else {
            reset();
        }

        Stereo output = dry;
        if (smoothedMix == 1.)
            output = wet;
        else if (smoothedMix != 0.)
            for (std::size_t channel = 0; channel < output.size(); ++channel)
                output[channel] += static_cast<float>(smoothedMix) * (wet[channel] - output[channel]);
        if (smoothedOut != 1.)
            for (auto& sample : output)
                sample *= static_cast<float>(smoothedOut);
        return output;
    }

private:
    static void advance(double& value, double target, double ramp) {
        value += (target - value) * ramp;
        if (std::abs(target - value) < 1.e-7)
            value = target;
    }

    void beginFade() {
        fadeFrom = lastWet;
        fade = fadeSize;
    }

    std::vector<Stereo> grain;
    std::size_t captureSize{}, captured{}, readPosition{}, fade{}, fadeSize{};
    double sampleRate{48000.}, previousBeat{};
    double smoothedIn{1.}, smoothedOut{1.}, smoothedMix{};
    Stereo lastWet{}, fadeFrom{};
    bool primed{};
};
}
