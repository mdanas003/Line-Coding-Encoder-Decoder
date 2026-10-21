// =============================================================================
// modulation.hpp
// -----------------------------------------------------------------------------
// Part 3 of the assignment: ANALOG -> DIGITAL conversion.
//
//   PCM (Pulse Code Modulation):
//     1. sample the analog signal at fs samples per second
//     2. quantize each sample to one of L = 2^n levels (uniform, mid-rise)
//     3. write each level number as an n-bit binary code
//     Decoder: read n bits -> level number -> level value, and hold it until
//     the next sample. This gives the STAIRCASE signal.
//
//   DM (Delta Modulation):
//     Send only 1 bit per sample: 1 = "go up by delta", 0 = "go down by delta".
//     The encoder keeps its own copy of the staircase and compares each new
//     sample with it. The decoder rebuilds the same staircase from the bits.
//     Danger: if the signal rises faster than delta*fs, the staircase cannot
//     follow ("slope overload").
// =============================================================================
#pragma once

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

#include "data_generator.hpp"

namespace modulation {

constexpr double kPi = 3.14159265358979323846;

// An analog test signal: a name and a function x(t) built from amplitude A
// and frequency f. All shapes stay inside [-A, +A].
struct AnalogShape {
    std::string name;
    std::function<double(double A, double f, double t)> x;
};

inline std::vector<AnalogShape> analogShapes() {
    return {
        {"Sine      x(t) = A sin(2 pi f t)",
         [](double A, double f, double t) { return A * std::sin(2 * kPi * f * t); }},
        {"Two-tone  x(t) = A/2 [sin(2 pi f t) + sin(2 pi 3f t)]",
         [](double A, double f, double t) {
             return A / 2 * (std::sin(2 * kPi * f * t) + std::sin(2 * kPi * 3 * f * t));
         }},
        {"Triangle  x(t) = A (2/pi) asin(sin(2 pi f t))",
         [](double A, double f, double t) {
             return A * (2 / kPi) * std::asin(std::sin(2 * kPi * f * t));
         }},
    };
}

// Sample x(t) at fs samples/second for `duration` seconds.
struct Samples {
    std::vector<double> t, x;
};

inline Samples sample(const std::function<double(double)>& x, double duration, double fs) {
    Samples s;
    const int n = static_cast<int>(std::round(duration * fs));
    for (int k = 0; k < n; ++k) {
        s.t.push_back(k / fs);
        s.x.push_back(x(k / fs));
    }
    return s;
}

// ------------------------------- PCM -----------------------------------------
// Step size delta = 2A / L. A sample x falls into level q = floor((x + A) / delta),
// and that level is reconstructed at its MIDDLE: -A + (q + 0.5) * delta.
// So the quantization error is never larger than delta / 2.
inline Bits pcmEncode(const std::vector<double>& x, double A, int bitsPerSample) {
    const int L = 1 << bitsPerSample;
    const double delta = 2 * A / L;
    Bits bits;
    for (double value : x) {
        int q = static_cast<int>(std::floor((value + A) / delta));
        q = std::clamp(q, 0, L - 1);  // x = +A exactly would give q = L
        for (int b = bitsPerSample - 1; b >= 0; --b)  // most significant bit first
            bits += ((q >> b) & 1) ? '1' : '0';
    }
    return bits;
}

inline std::vector<double> pcmDecode(const Bits& bits, double A, int bitsPerSample) {
    const double delta = 2 * A / (1 << bitsPerSample);
    std::vector<double> staircase;
    for (size_t i = 0; i + bitsPerSample <= bits.size(); i += bitsPerSample) {
        int q = 0;
        for (int b = 0; b < bitsPerSample; ++b) q = q * 2 + (bits[i + b] - '0');
        staircase.push_back(-A + (q + 0.5) * delta);
    }
    return staircase;
}

// -------------------------------- DM -----------------------------------------
inline Bits dmEncode(const std::vector<double>& x, double delta) {
    Bits bits;
    double staircase = 0;  // encoder's copy of what the decoder will rebuild
    for (double value : x) {
        const bool up = value > staircase;
        bits += up ? '1' : '0';
        staircase += up ? delta : -delta;
    }
    return bits;
}

inline std::vector<double> dmDecode(const Bits& bits, double delta) {
    std::vector<double> staircase;
    double level = 0;
    for (char b : bits) {
        level += (b == '1') ? delta : -delta;
        staircase.push_back(level);
    }
    return staircase;
}

// Largest |dx/dt| of the signal, estimated with small finite differences.
// DM avoids slope overload when delta * fs >= this value.
inline double maxSlope(const std::function<double(double)>& x, double duration) {
    const int steps = 20000;
    const double dt = duration / steps;
    double best = 0;
    for (int k = 0; k < steps; ++k)
        best = std::max(best, std::fabs(x((k + 1) * dt) - x(k * dt)) / dt);
    return best;
}

// Signal-to-quantization-noise ratio in dB: 10 log10(signal power / error power).
inline double sqnrDb(const std::vector<double>& x, const std::vector<double>& xq) {
    double signal = 0, noise = 0;
    for (size_t i = 0; i < x.size() && i < xq.size(); ++i) {
        signal += x[i] * x[i];
        noise += (x[i] - xq[i]) * (x[i] - xq[i]);
    }
    return noise == 0 ? 999.0 : 10 * std::log10(signal / noise);
}

}  // namespace modulation
