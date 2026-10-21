// =============================================================================
// line_coding.hpp
// -----------------------------------------------------------------------------
// Part 2 of the assignment: LINE CODING, SCRAMBLING and their DECODERS.
//
// Design (no giant if/else or switch):
//   Every scheme is a class derived from the abstract class LineCoder.
//   The program keeps a list (vector) of LineCoder objects and calls
//   coder->encode(...) / coder->decode(...) through virtual functions
//   (runtime polymorphism). Adding a new scheme = adding one small class.
//
// How a signal is represented:
//   Each bit interval is split into two HALVES. For each half we store the
//   voltage level: +1 (= +V), 0, or -1 (= -V).
//   NRZ / AMI use the same level in both halves; Manchester schemes change
//   level in the middle of the bit, which is why we need halves at all.
//
// How the decoder works ("decode from the signal, not from the data"):
//   1. synthesize()        turns the levels into a sampled waveform (volts vs time)
//   2. saveCsv()           writes that waveform to output/signal.csv
//   3. loadCsv()           the decoder reads ONLY that file back
//   4. measureHalfLevels() samples the voltage at 1/4 and 3/4 of every bit
//                          and compares it with thresholds +V/2 and -V/2
//   5. coder->decode()     turns the measured levels back into bits
//
// Conventions (Forouzan, "Data Communications and Networking"):
//   NRZ-L       : 0 -> +V, 1 -> -V
//   NRZ-I       : 1 -> invert the level, 0 -> keep it (line starts at +V)
//   Manchester  : 0 -> high-to-low,  1 -> low-to-high (IEEE 802.3)
//   Diff. Manch.: always a transition in the middle;
//                 0 -> extra transition at the start, 1 -> none
//   AMI         : 0 -> 0 V, 1 -> alternately +V, -V (first 1 is +V)
//   B8ZS        : 8 zeros  -> 000VB0VB
//   HDB3        : 4 zeros  -> 000V (odd pulses since last substitution)
//                             B00V (even pulses since last substitution)
//   V = violation (same polarity as the previous pulse)
//   B = bipolar   (opposite polarity, a normal AMI pulse)
// =============================================================================
#pragma once

#include <algorithm>
#include <fstream>
#include <functional>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "data_generator.hpp"

// The output of every encoder.
struct EncodedSignal {
    std::vector<int> halfLevels;    // 2 entries per bit: level of 1st and 2nd half
    std::vector<bool> substituted;  // 1 entry per bit: true if a scrambler replaced it
};

// -----------------------------------------------------------------------------
// The common interface. Each scheme overrides these virtual functions.
// -----------------------------------------------------------------------------
class LineCoder {
public:
    virtual ~LineCoder() = default;
    virtual std::string name() const = 0;
    virtual std::string rule() const = 0;  // one-line explanation shown in the menu
    virtual EncodedSignal encode(const Bits& bits) const = 0;
    virtual Bits decode(const std::vector<int>& halfLevels) const = 0;
    virtual bool canBeScrambled() const { return false; }  // only AMI says yes
};

// =============================================================================
// Small helpers shared by several schemes
// =============================================================================
namespace detail {

// Make an EncodedSignal where both halves of each bit carry the same level.
inline EncodedSignal fromBitLevels(const std::vector<int>& levels,
                                   std::vector<bool> substituted = {}) {
    EncodedSignal e;
    e.halfLevels.reserve(levels.size() * 2);
    for (int l : levels) {
        e.halfLevels.push_back(l);
        e.halfLevels.push_back(l);
    }
    if (substituted.empty()) substituted.assign(levels.size(), false);
    e.substituted = std::move(substituted);
    return e;
}

// Take the first-half level of every bit (enough for NRZ and AMI-type codes).
inline std::vector<int> firstHalves(const std::vector<int>& halfLevels) {
    std::vector<int> levels(halfLevels.size() / 2);
    for (size_t i = 0; i < levels.size(); ++i) levels[i] = halfLevels[2 * i];
    return levels;
}

// zerosFrom[i] = how many consecutive '0' bits start at position i.
// Computed right-to-left in ONE pass, so the scramblers can ask
// "do 8 zeros start here?" in O(1) instead of looking ahead each time.
inline std::vector<int> zerosFrom(const Bits& bits) {
    std::vector<int> z(bits.size() + 1, 0);
    for (int i = static_cast<int>(bits.size()) - 1; i >= 0; --i)
        z[i] = (bits[i] == '0') ? z[i + 1] + 1 : 0;
    return z;
}

// Generic decoder for bipolar codes (AMI, B8ZS, HDB3).
// Walk through the levels; a pulse with the SAME polarity as the previous pulse
// is a "bipolar violation". Plain AMI never makes one, so a violation must come
// from a scrambler. `onViolation(i, out)` undoes that substitution and returns
// the index where normal decoding continues.
inline Bits decodeBipolar(const std::vector<int>& levels,
                          const std::function<size_t(size_t, Bits&)>& onViolation) {
    Bits out(levels.size(), '0');
    int lastPulse = -1;  // same starting assumption as the encoders
    for (size_t i = 0; i < levels.size();) {
        if (levels[i] == 0) { ++i; continue; }               // 0 V -> bit 0
        if (levels[i] == lastPulse && onViolation) {           // violation found
            const size_t next = onViolation(i, out);
            if (levels[next - 1] != 0) lastPulse = levels[next - 1];
            i = next;
            continue;
        }
        out[i] = '1';                                          // normal pulse -> bit 1
        lastPulse = levels[i];
        ++i;
    }
    return out;
}

}  // namespace detail

// =============================================================================
// The five basic line codes
// =============================================================================
class NrzL : public LineCoder {
public:
    std::string name() const override { return "NRZ-L"; }
    std::string rule() const override { return "level shows the bit: 0 -> +V, 1 -> -V"; }

    EncodedSignal encode(const Bits& bits) const override {
        std::vector<int> levels;
        for (char b : bits) levels.push_back(b == '0' ? +1 : -1);
        return detail::fromBitLevels(levels);
    }
    Bits decode(const std::vector<int>& halfLevels) const override {
        Bits out;
        for (int l : detail::firstHalves(halfLevels)) out += (l > 0) ? '0' : '1';
        return out;
    }
};

class NrzI : public LineCoder {
public:
    std::string name() const override { return "NRZ-I"; }
    std::string rule() const override { return "1 -> invert the level, 0 -> no change"; }

    EncodedSignal encode(const Bits& bits) const override {
        std::vector<int> levels;
        int level = +1;  // assumption: the line rests at +V before the first bit
        for (char b : bits) {
            if (b == '1') level = -level;
            levels.push_back(level);
        }
        return detail::fromBitLevels(levels);
    }
    Bits decode(const std::vector<int>& halfLevels) const override {
        Bits out;
        int previous = +1;
        for (int l : detail::firstHalves(halfLevels)) {
            out += (l != previous) ? '1' : '0';  // a change means 1
            previous = l;
        }
        return out;
    }
};

class Manchester : public LineCoder {
public:
    std::string name() const override { return "Manchester"; }
    std::string rule() const override { return "0 -> high-to-low, 1 -> low-to-high (mid-bit)"; }

    EncodedSignal encode(const Bits& bits) const override {
        EncodedSignal e;
        for (char b : bits) {
            const int first = (b == '0') ? +1 : -1;
            e.halfLevels.push_back(first);
            e.halfLevels.push_back(-first);
        }
        e.substituted.assign(bits.size(), false);
        return e;
    }
    Bits decode(const std::vector<int>& h) const override {
        Bits out;
        for (size_t i = 0; i + 1 < h.size(); i += 2)
            out += (h[i] < h[i + 1]) ? '1' : '0';  // rising edge in the middle = 1
        return out;
    }
};

class DifferentialManchester : public LineCoder {
public:
    std::string name() const override { return "Differential Manchester"; }
    std::string rule() const override { return "always mid-bit transition; 0 -> extra one at start"; }

    EncodedSignal encode(const Bits& bits) const override {
        EncodedSignal e;
        int previousEnd = +1;  // assumption: line at +V before the first bit
        for (char b : bits) {
            const int first = (b == '0') ? -previousEnd : previousEnd;  // 0: jump at start
            e.halfLevels.push_back(first);
            e.halfLevels.push_back(-first);                              // mid-bit jump
            previousEnd = -first;
        }
        e.substituted.assign(bits.size(), false);
        return e;
    }
    Bits decode(const std::vector<int>& h) const override {
        Bits out;
        int previousEnd = +1;
        for (size_t i = 0; i + 1 < h.size(); i += 2) {
            out += (h[i] == previousEnd) ? '1' : '0';  // no jump at the start = 1
            previousEnd = h[i + 1];
        }
        return out;
    }
};

class Ami : public LineCoder {
public:
    std::string name() const override { return "AMI"; }
    std::string rule() const override { return "0 -> 0 V, 1 -> alternately +V / -V"; }
    bool canBeScrambled() const override { return true; }

    EncodedSignal encode(const Bits& bits) const override {
        std::vector<int> levels;
        int lastPulse = -1;  // so the first 1 becomes +V
        for (char b : bits) {
            if (b == '1') lastPulse = -lastPulse;
            levels.push_back(b == '1' ? lastPulse : 0);
        }
        return detail::fromBitLevels(levels);
    }
    Bits decode(const std::vector<int>& halfLevels) const override {
        return detail::decodeBipolar(detail::firstHalves(halfLevels), nullptr);
    }
};

// =============================================================================
// Scrambling schemes (AMI + substitution of long zero runs). Both run in O(n).
// =============================================================================
class B8zs : public LineCoder {
public:
    std::string name() const override { return "AMI + B8ZS"; }
    std::string rule() const override { return "every 8 zeros -> 000VB0VB"; }

    EncodedSignal encode(const Bits& bits) const override {
        const std::vector<int> zeros = detail::zerosFrom(bits);
        std::vector<int> levels;
        std::vector<bool> substituted;
        int lastPulse = -1;

        for (size_t i = 0; i < bits.size();) {
            if (zeros[i] >= 8) {
                // V = same polarity as last pulse, B = opposite.
                // With last pulse p the pattern 000VB0VB becomes 0 0 0 p -p 0 -p p.
                const int p = lastPulse;
                const int pattern[8] = {0, 0, 0, p, -p, 0, -p, p};
                levels.insert(levels.end(), pattern, pattern + 8);
                substituted.insert(substituted.end(), 8, true);
                i += 8;  // last pulse sent is p again, so lastPulse is unchanged
                continue;
            }
            if (bits[i] == '1') lastPulse = -lastPulse;
            levels.push_back(bits[i] == '1' ? lastPulse : 0);
            substituted.push_back(false);
            ++i;
        }
        return detail::fromBitLevels(levels, substituted);
    }

    Bits decode(const std::vector<int>& halfLevels) const override {
        // The first violation sits at position 3 of the 8-bit pattern:
        // bits i-3 .. i+4 were all zeros originally.
        return detail::decodeBipolar(detail::firstHalves(halfLevels), [](size_t i, Bits& out) {
            const size_t end = std::min(i + 5, out.size());
            std::fill(out.begin() + i, out.begin() + end, '0');
            return end;
        });
    }
};

class Hdb3 : public LineCoder {
public:
    std::string name() const override { return "AMI + HDB3"; }
    std::string rule() const override { return "every 4 zeros -> 000V (odd pulses) or B00V (even)"; }

    EncodedSignal encode(const Bits& bits) const override {
        const std::vector<int> zeros = detail::zerosFrom(bits);
        std::vector<int> levels;
        std::vector<bool> substituted;
        int lastPulse = -1;
        int pulsesSinceSubstitution = 0;

        for (size_t i = 0; i < bits.size();) {
            if (zeros[i] >= 4) {
                // Odd count  -> 000V : V copies the last pulse.
                // Even count -> B00V : B is a normal (opposite) pulse, V copies B.
                // Either way the number of pulses between violations stays odd,
                // so consecutive V's alternate and the DC level stays zero.
                const bool odd = pulsesSinceSubstitution % 2 == 1;
                const int b = odd ? 0 : -lastPulse;
                const int v = odd ? lastPulse : b;
                const int pattern[4] = {b, 0, 0, v};
                levels.insert(levels.end(), pattern, pattern + 4);
                substituted.insert(substituted.end(), 4, true);
                lastPulse = v;
                pulsesSinceSubstitution = 0;
                i += 4;
                continue;
            }
            if (bits[i] == '1') {
                lastPulse = -lastPulse;
                ++pulsesSinceSubstitution;
            }
            levels.push_back(bits[i] == '1' ? lastPulse : 0);
            substituted.push_back(false);
            ++i;
        }
        return detail::fromBitLevels(levels, substituted);
    }

    Bits decode(const std::vector<int>& halfLevels) const override {
        // The violation V is the LAST bit of the 4-bit pattern, so bits i-3 .. i
        // were zeros (this also clears a B that was first read as a 1).
        return detail::decodeBipolar(detail::firstHalves(halfLevels), [](size_t i, Bits& out) {
            const size_t begin = (i >= 3) ? i - 3 : 0;
            std::fill(out.begin() + begin, out.begin() + i + 1, '0');
            return i + 1;
        });
    }
};

// Registries: the menus are built from these lists, not from switch statements.
inline std::vector<std::unique_ptr<LineCoder>> makeLineCoders() {
    std::vector<std::unique_ptr<LineCoder>> list;
    list.push_back(std::make_unique<NrzL>());
    list.push_back(std::make_unique<NrzI>());
    list.push_back(std::make_unique<Manchester>());
    list.push_back(std::make_unique<DifferentialManchester>());
    list.push_back(std::make_unique<Ami>());
    return list;
}

inline std::vector<std::unique_ptr<LineCoder>> makeScramblers() {
    std::vector<std::unique_ptr<LineCoder>> list;
    list.push_back(std::make_unique<B8zs>());
    list.push_back(std::make_unique<Hdb3>());
    return list;
}

// =============================================================================
// Waveform: the "image" of the signal - voltage samples over time
// =============================================================================
struct Waveform {
    std::vector<double> t;  // time, in bit periods (bit k spans [k, k+1))
    std::vector<double> v;  // voltage in volts
    int samplesPerBit = 0;
};

// Turn half-bit levels into samples. V is the pulse amplitude in volts.
inline Waveform synthesize(const std::vector<int>& halfLevels, int samplesPerBit = 20,
                           double V = 1.0) {
    Waveform w;
    w.samplesPerBit = samplesPerBit;
    const int perHalf = samplesPerBit / 2;
    for (size_t k = 0; k < halfLevels.size(); ++k)
        for (int s = 0; s < perHalf; ++s) {
            const size_t index = k * perHalf + s;
            w.t.push_back(static_cast<double>(index) / samplesPerBit);
            w.v.push_back(halfLevels[k] * V);
        }
    return w;
}

// The receiver side: measure the voltage in the middle of each half-bit and
// decide which level it is, using thresholds at +V/2 and -V/2.
inline std::vector<int> measureHalfLevels(const Waveform& w, double V = 1.0) {
    std::vector<int> levels;
    const int spb = w.samplesPerBit;
    const size_t bitCount = w.v.size() / spb;
    for (size_t b = 0; b < bitCount; ++b)
        for (int half = 0; half < 2; ++half) {
            const double x = w.v[b * spb + spb / 4 + half * spb / 2];
            levels.push_back(x > V / 2 ? +1 : (x < -V / 2 ? -1 : 0));
        }
    return levels;
}

inline void saveCsv(const Waveform& w, const std::string& path) {
    std::ofstream file(path);
    if (!file) throw std::runtime_error("cannot write " + path);
    file << "t_bits,volts\n" << std::fixed << std::setprecision(4);
    for (size_t i = 0; i < w.v.size(); ++i) file << w.t[i] << ',' << w.v[i] << '\n';
}

// Read a waveform back. The samples-per-bit value is NOT stored in the file:
// it is recovered from the time axis (number of samples with t < 1 bit).
inline Waveform loadCsv(const std::string& path) {
    std::ifstream file(path);
    if (!file) throw std::runtime_error("cannot read " + path);
    Waveform w;
    std::string line;
    std::getline(file, line);  // skip the header
    while (std::getline(file, line)) {
        std::stringstream row(line);
        double t = 0, v = 0;
        char comma = 0;
        if (row >> t >> comma >> v) {
            w.t.push_back(t);
            w.v.push_back(v);
        }
    }
    w.samplesPerBit = static_cast<int>(
        std::count_if(w.t.begin(), w.t.end(), [](double t) { return t < 1.0 - 1e-9; }));
    if (w.samplesPerBit < 4) throw std::runtime_error("waveform file has too few samples");
    return w;
}
