// =============================================================================
// data_generator.hpp
// -----------------------------------------------------------------------------
// Part 1 of the assignment: the DIGITAL DATA GENERATOR and stream analysis.
//
//   * randomBits()          - a completely random bit stream
//   * randomWithZeroRuns()  - a random stream with fixed runs of 4 or 8 zeros
//                             planted in it (useful to test B8ZS / HDB3)
//   * longestPalindrome()   - Manacher's algorithm, O(n) time
//   * longestZeroRun()      - one left-to-right pass, O(n) time
//
// A bit stream is stored as a std::string of '0' / '1' characters.
// Why a string? It is easy to print, easy to type in, and string algorithms
// (like Manacher's) work on it directly.
// =============================================================================
#pragma once

#include <algorithm>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

using Bits = std::string;  // e.g. "01001100"

namespace datagen {

// One shared random-number engine (Mersenne Twister), seeded once from the OS.
inline std::mt19937& rng() {
    static std::mt19937 engine(std::random_device{}());
    return engine;
}

// Uniform random integer in [lo, hi] (both ends included).
inline int randomInt(int lo, int hi) {
    std::uniform_int_distribution<int> dist(lo, hi);
    return dist(rng());
}

// -----------------------------------------------------------------------------
// Completely random stream: every bit is an independent fair coin flip.
// -----------------------------------------------------------------------------
inline Bits randomBits(int n) {
    std::bernoulli_distribution coin(0.5);
    Bits bits(n, '0');
    for (char& b : bits) b = coin(rng()) ? '1' : '0';
    return bits;
}

// -----------------------------------------------------------------------------
// Random stream with `runs` fixed sub-sequences of `runLength` zeros.
//
// Idea: split the stream into `runs` equal slots and drop one block of zeros
// at a random offset inside each slot. Because each block stays inside its own
// slot, blocks never overlap, and the whole thing is O(n).
// -----------------------------------------------------------------------------
inline Bits randomWithZeroRuns(int n, int runLength, int runs) {
    if (runs <= 0 || runLength <= 0 || runs * runLength > n)
        throw std::invalid_argument("stream too short for that many zero runs");

    Bits bits = randomBits(n);
    const int slot = n / runs;  // slot >= runLength is guaranteed by the check above
    for (int r = 0; r < runs; ++r) {
        const int start = r * slot + randomInt(0, slot - runLength);
        std::fill(bits.begin() + start, bits.begin() + start + runLength, '0');
    }
    return bits;
}

// -----------------------------------------------------------------------------
// LONGEST PALINDROMIC SUBSTRING - Manacher's algorithm, O(n).
//
// Brute force tries every centre and expands, which is O(n^2).
// Manacher's trick: if position i lies inside a palindrome we already found
// (centre c, right edge r), then the mirror position 2c - i has a palindrome
// that we ALREADY measured. Position i gets at least the same radius (capped at
// the right edge r), so we never compare the same characters twice.
//
// To handle even and odd lengths the same way, we put '#' between characters:
//     "0110"  ->  "#0#1#1#0#"
// Now every palindrome has odd length and a single centre.
// -----------------------------------------------------------------------------
struct Segment {
    int start = 0;   // index of the first bit
    int length = 0;  // number of bits
};

inline Segment longestPalindrome(const Bits& s) {
    if (s.empty()) return {};

    std::string t = "#";
    for (char c : s) { t += c; t += '#'; }
    const int m = static_cast<int>(t.size());

    std::vector<int> radius(m, 0);  // radius[i] = palindrome radius in t centred at i
    int centre = 0, right = 0;      // the palindrome that reaches furthest right so far
    int bestCentre = 0, bestRadius = 0;

    for (int i = 0; i < m; ++i) {
        // 1. Reuse the mirror's answer if i is inside the known palindrome.
        if (i < right) radius[i] = std::min(right - i, radius[2 * centre - i]);

        // 2. Try to grow past what we already know.
        while (i - radius[i] - 1 >= 0 && i + radius[i] + 1 < m &&
               t[i - radius[i] - 1] == t[i + radius[i] + 1])
            ++radius[i];

        // 3. If this palindrome reaches further right, it becomes the new reference.
        if (i + radius[i] > right) { centre = i; right = i + radius[i]; }

        if (radius[i] > bestRadius) { bestRadius = radius[i]; bestCentre = i; }
    }
    // In t, the radius equals the length of the palindrome in the original string.
    return {(bestCentre - bestRadius) / 2, bestRadius};
}

// -----------------------------------------------------------------------------
// LONGEST RUN OF ZEROS - single pass, O(n).
// This is what the scramblers care about: long zero runs make the receiver
// lose its clock, so B8ZS / HDB3 replace them.
// -----------------------------------------------------------------------------
inline Segment longestZeroRun(const Bits& s) {
    Segment best, current;
    for (int i = 0; i < static_cast<int>(s.size()); ++i) {
        if (s[i] == '0') {
            if (current.length == 0) current.start = i;
            ++current.length;
            if (current.length > best.length) best = current;
        } else {
            current.length = 0;
        }
    }
    return best;
}

}  // namespace datagen
