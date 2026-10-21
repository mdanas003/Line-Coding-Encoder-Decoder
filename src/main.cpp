// =============================================================================
// main.cpp - Digital Signal Encoder and Decoder (interactive program)
// -----------------------------------------------------------------------------
// Flow (as the assignment asks):
//
//   Main menu: digital input  or  analog input
//
//   Digital input:  type bits / random bits / random bits with zero runs
//                   -> show stream, longest palindrome, longest zero run
//                   -> line coding
//
//   Analog input:   choose shape + PCM or DM -> bits + decoder staircase
//                   -> show stream, longest palindrome -> line coding
//
//   Line coding:    NRZ-L, NRZ-I, Manchester, Diff. Manchester, AMI
//                   AMI -> "scrambling?" -> B8ZS or HDB3
//                   -> ASCII plot in terminal + SVG plot in output/report.html
//                   -> "decode?" -> the decoder reads output/signal.csv
//                      (the waveform only) and rebuilds the bits
//
// Build:  g++ -std=c++17 -O2 -Wall -Wextra -o encoder src/main.cpp
// Run:    ./encoder          (interactive)
//         ./encoder --test   (automatic self-test of every encoder/decoder)
// =============================================================================
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX  // stop windows.h from defining min/max macros
#endif
#include <windows.h>
#endif

#include "data_generator.hpp"
#include "line_coding.hpp"
#include "modulation.hpp"
#include "plot.hpp"

namespace {

const std::string kOutputDir = "output";
const std::string kSignalFile = kOutputDir + "/signal.csv";
const std::string kReportFile = kOutputDir + "/report.html";

// ANSI colour codes for a nicer terminal interface.
const char* const CYAN = "\033[36m";
const char* const GREEN = "\033[32m";
const char* const RED = "\033[31m";
const char* const BOLD = "\033[1m";
const char* const RESET = "\033[0m";

// Thrown when the input ends (e.g. Ctrl+Z / Ctrl+D or a finished input file).
struct InputClosed {};

// =============================================================================
// Input helpers. They re-ask until the answer is valid. An empty answer
// selects the default value shown in [brackets].
// =============================================================================
std::string readLine(const std::string& prompt) {
    std::cout << prompt;
    std::string line;
    if (!std::getline(std::cin, line)) throw InputClosed{};
    return line;
}

int readInt(const std::string& prompt, int lo, int hi, int def) {
    while (true) {
        const std::string s = readLine(prompt + " [" + std::to_string(def) + "]: ");
        if (s.empty()) return def;
        std::istringstream in(s);
        int v;
        if (in >> v && v >= lo && v <= hi) return v;
        std::cout << RED << "  Please enter a whole number from " << lo << " to " << hi << RESET
                  << '\n';
    }
}

double readDouble(const std::string& prompt, double lo, double hi, double def) {
    while (true) {
        std::ostringstream d;
        d << def;
        const std::string s = readLine(prompt + " [" + d.str() + "]: ");
        if (s.empty()) return def;
        std::istringstream in(s);
        double v;
        if (in >> v && v >= lo && v <= hi) return v;
        std::cout << RED << "  Please enter a number from " << lo << " to " << hi << RESET << '\n';
    }
}

bool askYesNo(const std::string& prompt) {
    while (true) {
        const std::string s = readLine(prompt + " (y/n): ");
        if (!s.empty() && (s[0] == 'y' || s[0] == 'Y')) return true;
        if (!s.empty() && (s[0] == 'n' || s[0] == 'N')) return false;
    }
}

// Print a numbered menu and return the 0-based index of the choice.
int choose(const std::string& title, const std::vector<std::string>& options) {
    std::cout << '\n' << BOLD << CYAN << title << RESET << '\n';
    for (size_t i = 0; i < options.size(); ++i)
        std::cout << "  " << i + 1 << ") " << options[i] << '\n';
    return readInt("Your choice", 1, static_cast<int>(options.size()), 1) - 1;
}

// Print a long bit stream in rows of 64 with the index of each row.
std::string wrapBits(const Bits& bits, size_t perRow = 64) {
    std::ostringstream s;
    for (size_t i = 0; i < bits.size(); i += perRow)
        s << "  [" << i << "] " << bits.substr(i, perRow) << '\n';
    return s.str();
}

// One symbol per level: + for +V, 0 for 0 V, - for -V.
// Biphase codes (Manchester) show both halves, e.g. "+-".
std::string levelText(const EncodedSignal& e) {
    auto symbol = [](int l) { return l > 0 ? '+' : (l < 0 ? '-' : '0'); };
    bool biphase = false;
    for (size_t i = 0; i + 1 < e.halfLevels.size(); i += 2)
        biphase = biphase || e.halfLevels[i] != e.halfLevels[i + 1];
    std::string s;
    for (size_t i = 0; i + 1 < e.halfLevels.size(); i += 2) {
        s += symbol(e.halfLevels[i]);
        if (biphase) { s += symbol(e.halfLevels[i + 1]); s += ' '; }
    }
    return s;
}

void openInBrowser(const std::string& path) {
#ifdef _WIN32
    const std::string cmd = "start \"\" \"" + path + "\"";
#elif __APPLE__
    const std::string cmd = "open \"" + path + "\"";
#else
    const std::string cmd = "xdg-open \"" + path + "\" >/dev/null 2>&1 &";
#endif
    if (std::system(cmd.c_str()) != 0) std::cout << "Open " << path << " in a browser.\n";
}

// =============================================================================
// Stream analysis: shown for every data stream (digital or from PCM/DM).
// =============================================================================
void analyseStream(const Bits& bits, plot::Report& report) {
    const datagen::Segment pal = datagen::longestPalindrome(bits);   // O(n) Manacher
    const datagen::Segment zeros = datagen::longestZeroRun(bits);    // O(n) one pass

    std::ostringstream s;
    s << "Data stream (" << bits.size() << " bits):\n" << wrapBits(bits);
    s << "\nLongest palindrome: length " << pal.length << ", bits " << pal.start << ".."
      << pal.start + pal.length - 1 << "\n  " << bits.substr(pal.start, pal.length) << '\n';
    s << "Longest run of zeros: length " << zeros.length;
    if (zeros.length > 0) s << ", bits " << zeros.start << ".." << zeros.start + zeros.length - 1;
    s << '\n';

    std::cout << '\n' << BOLD << "=== Data stream ===" << RESET << '\n' << s.str();
    report.addText("Data stream and analysis", s.str());
}

// Show one encoded signal in the terminal and add it to the HTML report.
void showSignal(const std::string& title, const Bits& bits, const EncodedSignal& e,
                plot::Report& report) {
    std::cout << '\n' << BOLD << "=== " << title << " ===" << RESET << '\n';
    plot::asciiDigital(std::cout, bits, e);
    std::cout << "Levels: " << levelText(e) << '\n';
    report.add(title, "<div class='scroll'>" + plot::svgDigital(bits, e) +
                          "</div><pre>Levels: " + levelText(e) + "</pre>");
}

// =============================================================================
// Line coding stage (+ optional scrambling, + optional decoding)
// =============================================================================
void lineCodingStage(const Bits& bits, plot::Report& report) {
    const auto coders = makeLineCoders();
    std::vector<std::string> names;
    for (const auto& c : coders) names.push_back(c->name() + "  -  " + c->rule());
    const LineCoder* coder = coders[choose("Choose a line coding scheme", names)].get();

    // AMI only: ask about scrambling, then about the type.
    std::vector<std::unique_ptr<LineCoder>> scramblers;
    const LineCoder* scrambler = nullptr;
    if (coder->canBeScrambled() && askYesNo("\nDo you want scrambling?")) {
        scramblers = makeScramblers();
        std::vector<std::string> sNames;
        for (const auto& s : scramblers) sNames.push_back(s->name() + "  -  " + s->rule());
        scrambler = scramblers[choose("Choose the scrambling scheme", sNames)].get();
    }

    // Always show the plain signal; with scrambling also show the scrambled one.
    const EncodedSignal plain = coder->encode(bits);
    showSignal("Digital signal: " + coder->name(), bits, plain, report);

    const LineCoder* sent = coder;  // the scheme that is actually put on the line
    EncodedSignal line = plain;
    if (scrambler) {
        line = scrambler->encode(bits);
        showSignal("Scrambled signal: " + scrambler->name(), bits, line, report);
        sent = scrambler;
    }

    // Save the transmitted waveform. From here on the decoder uses only this file.
    saveCsv(synthesize(line.halfLevels), kSignalFile);
    std::cout << "Waveform samples saved to " << kSignalFile << '\n';

    if (askYesNo("\nDo you want to decode the signal?")) {
        const Waveform received = loadCsv(kSignalFile);         // read the "image"
        const std::vector<int> measured = measureHalfLevels(received);
        const Bits decoded = sent->decode(measured);

        const bool ok = decoded == bits;
        std::ostringstream s;
        s << "Decoder read " << received.v.size() << " samples (" << received.samplesPerBit
          << " samples per bit) and measured " << measured.size() / 2 << " bits.\n"
          << "Decoded stream:\n" << wrapBits(decoded)
          << (ok ? "MATCH: decoded stream equals the original.\n"
                 : "MISMATCH: decoded stream differs from the original.\n");
        std::cout << '\n' << BOLD << "=== Decoder (" << sent->name() << ") ===" << RESET << '\n'
                  << (ok ? GREEN : RED) << s.str() << RESET;
        report.addText("Decoder output (" + sent->name() + ")", s.str());
    }
}

// =============================================================================
// Digital input path
// =============================================================================
Bits typedBits() {
    while (true) {
        Bits bits;
        bool valid = true;
        for (char c : readLine("Enter bits (only 0 and 1, spaces allowed): ")) {
            if (c == '0' || c == '1') bits += c;
            else if (c != ' ') valid = false;
        }
        if (valid && !bits.empty()) return bits;
        std::cout << RED << "  Only 0, 1 and spaces are allowed." << RESET << '\n';
    }
}

Bits randomStream() { return datagen::randomBits(readInt("Number of bits", 1, 100000, 32)); }

Bits randomStreamWithZeros() {
    const int n = readInt("Number of bits", 8, 100000, 48);
    const int run = choose("Length of the fixed zero sub-sequence",
                           {"8 consecutive zeros", "4 consecutive zeros"}) == 0 ? 8 : 4;
    const int runs = readInt("How many zero sub-sequences", 1, n / run, std::min(2, n / run));
    return datagen::randomWithZeroRuns(n, run, runs);
}

void digitalFlow() {
    // The menu is a table of (label, generator function) - no switch needed.
    const std::vector<std::pair<std::string, std::function<Bits()>>> sources = {
        {"Type the bits myself", typedBits},
        {"Completely random data", randomStream},
        {"Random data with fixed zero sub-sequences (4 or 8 zeros)", randomStreamWithZeros},
    };
    std::vector<std::string> names;
    for (const auto& s : sources) names.push_back(s.first);
    const Bits bits = sources[choose("Digital data source", names)].second();

    plot::Report report;
    analyseStream(bits, report);
    lineCodingStage(bits, report);
    report.write(kReportFile);
    std::cout << "\nFull graphical report: " << kReportFile << '\n';
    if (askYesNo("Open the report in your browser?")) openInBrowser(kReportFile);
}

// =============================================================================
// Analog input path (PCM or DM)
// =============================================================================
struct AnalogInput {
    std::function<double(double)> x;  // x(t) in volts
    double A = 1, f = 1, duration = 1, fs = 1;
};

// Result of one A->D technique: the bits and the decoder's staircase.
struct Conversion {
    Bits bits;
    std::vector<double> staircase;
    std::string summary;
};

Conversion runPcm(const AnalogInput& in, const modulation::Samples& s) {
    const int n = readInt("Bits per sample n (L = 2^n levels)", 1, 12, 3);
    Conversion c;
    c.bits = modulation::pcmEncode(s.x, in.A, n);
    c.staircase = modulation::pcmDecode(c.bits, in.A, n);  // decoder: bits -> staircase
    std::ostringstream o;
    o << "PCM: " << s.x.size() << " samples x " << n << " bits = " << c.bits.size() << " bits\n"
      << "Levels L = " << (1 << n) << ", step = " << 2 * in.A / (1 << n) << " V, "
      << "SQNR = " << modulation::sqnrDb(s.x, c.staircase) << " dB (theory ~ 6.02n + 1.76 = "
      << 6.02 * n + 1.76 << " dB for a full-scale sine)\n";
    c.summary = o.str();
    return c;
}

Conversion runDm(const AnalogInput& in, const modulation::Samples& s) {
    const double slope = modulation::maxSlope(in.x, in.duration);
    const double minDelta = slope / in.fs;  // smallest delta without slope overload
    const double delta = readDouble("Step size delta (V)", 1e-6, 10 * in.A, minDelta * 1.2);
    Conversion c;
    c.bits = modulation::dmEncode(s.x, delta);
    c.staircase = modulation::dmDecode(c.bits, delta);  // decoder: bits -> staircase
    std::ostringstream o;
    o << "DM: " << s.x.size() << " samples -> " << c.bits.size() << " bits (1 bit per sample)\n"
      << "delta = " << delta << " V, max slope of x(t) = " << slope << " V/s\n"
      << (delta * in.fs >= slope ? "No slope overload (delta*fs >= max slope)\n"
                                 : "WARNING: slope overload (delta*fs < max slope)\n")
      << "SQNR = " << modulation::sqnrDb(s.x, c.staircase) << " dB\n";
    c.summary = o.str();
    return c;
}

void analogFlow() {
    const auto shapes = modulation::analogShapes();
    std::vector<std::string> names;
    for (const auto& sh : shapes) names.push_back(sh.name);
    const auto& shape = shapes[choose("Analog signal", names)];

    AnalogInput in;
    in.A = readDouble("Amplitude A (V)", 0.001, 1000, 1.0);
    in.f = readDouble("Frequency f (Hz)", 0.001, 1e6, 1000);
    const double cycles = readDouble("Number of cycles to show", 0.25, 50, 1);
    in.duration = cycles / in.f;
    const double maxF = shape.name.rfind("Two", 0) == 0 ? 3 * in.f : in.f;  // highest tone
    in.fs = readDouble("Sampling rate fs (samples/s), Nyquist needs > " + std::to_string(2 * maxF),
                       1e-3, 1e9, 16 * in.f);
    if (in.fs < 2 * maxF)
        std::cout << RED << "  Warning: fs is below the Nyquist rate - aliasing will occur."
                  << RESET << '\n';

    const double A = in.A, f = in.f;
    in.x = [&shape, A, f](double t) { return shape.x(A, f, t); };
    const modulation::Samples s = modulation::sample(in.x, in.duration, in.fs);
    if (s.x.empty()) {
        std::cout << RED << "No samples - increase fs or the number of cycles." << RESET << '\n';
        return;
    }

    const std::vector<std::pair<std::string, std::function<Conversion()>>> methods = {
        {"PCM - Pulse Code Modulation", [&] { return runPcm(in, s); }},
        {"DM  - Delta Modulation", [&] { return runDm(in, s); }},
    };
    std::vector<std::string> mNames;
    for (const auto& m : methods) mNames.push_back(m.first);
    const int m = choose("Analog-to-digital technique", mNames);
    const Conversion c = methods[m].second();

    // Terminal output: summary + ASCII plot of samples and staircase.
    std::cout << '\n' << BOLD << "=== " << methods[m].first << " ===" << RESET << '\n'
              << c.summary << '\n';
    plot::asciiAnalog(std::cout, s.x, c.staircase);

    // HTML report: smooth curve (dense) + samples + decoder staircase.
    std::vector<double> dt, dx;
    for (int k = 0; k <= 800; ++k) {
        dt.push_back(in.duration * k / 800);
        dx.push_back(in.x(dt.back()));
    }
    plot::Report report;
    report.add(methods[m].first + " - blue: analog, dots: samples, red: decoder staircase",
               "<div class='scroll'>" +
                   plot::svgAnalog(dt, dx, s.t, s.x, c.staircase, in.duration) + "</div><pre>" +
                   c.summary + "</pre>");

    analyseStream(c.bits, report);
    lineCodingStage(c.bits, report);
    report.write(kReportFile);
    std::cout << "\nFull graphical report: " << kReportFile << '\n';
    if (askYesNo("Open the report in your browser?")) openInBrowser(kReportFile);
}

// =============================================================================
// Self-test: checks every part automatically (run with --test).
// =============================================================================
bool selfTest() {
    int checks = 0, failures = 0;
    auto check = [&](bool ok, const std::string& what) {
        ++checks;
        if (!ok) {
            ++failures;
            std::cout << RED << "  FAIL: " << what << RESET << '\n';
        }
    };

    // 1. Manacher must agree with brute force on many small random strings.
    for (int trial = 0; trial < 500; ++trial) {
        const Bits b = datagen::randomBits(datagen::randomInt(1, 40));
        int brute = 0;
        for (size_t i = 0; i < b.size(); ++i)
            for (size_t j = i; j < b.size(); ++j) {
                const std::string sub = b.substr(i, j - i + 1);
                if (std::equal(sub.begin(), sub.end(), sub.rbegin()))
                    brute = std::max(brute, static_cast<int>(sub.size()));
            }
        const datagen::Segment p = datagen::longestPalindrome(b);
        const std::string sub = b.substr(p.start, p.length);
        check(p.length == brute && std::equal(sub.begin(), sub.end(), sub.rbegin()),
              "palindrome on " + b);
    }

    // 2. Every coder: encode -> waveform -> CSV -> measure -> decode == original.
    std::vector<std::unique_ptr<LineCoder>> all = makeLineCoders();
    for (auto& s : makeScramblers()) all.push_back(std::move(s));
    const std::string tmp = kOutputDir + "/selftest.csv";
    for (const auto& coder : all)
        for (int trial = 0; trial < 300; ++trial) {
            const int n = datagen::randomInt(1, 120);
            const Bits b = (trial % 3 == 0) ? datagen::randomBits(n)
                           : (trial % 3 == 1 && n >= 16) ? datagen::randomWithZeroRuns(n, 8, n / 16)
                           : (n >= 8) ? datagen::randomWithZeroRuns(n, 4, n / 8)
                                      : Bits(n, '0');
            saveCsv(synthesize(coder->encode(b).halfLevels), tmp);
            check(coder->decode(measureHalfLevels(loadCsv(tmp))) == b,
                  coder->name() + " round trip on " + b);
        }

    // 3. Scrambled signals must not contain long zero-voltage runs.
    const auto scr = makeScramblers();
    const int maxZeros[2] = {7, 3};  // B8ZS, HDB3
    for (int i = 0; i < 2; ++i)
        for (int trial = 0; trial < 200; ++trial) {
            const Bits b = datagen::randomWithZeroRuns(64, 8, 4);
            Bits levels;
            for (int l : detail::firstHalves(scr[i]->encode(b).halfLevels)) levels += l ? '1' : '0';
            check(datagen::longestZeroRun(levels).length <= maxZeros[i],
                  scr[i]->name() + " left a long zero run in " + b);
        }

    // 4. PCM error <= step/2; DM decoder staircase follows a slow signal.
    for (int n = 1; n <= 10; ++n) {
        std::vector<double> x;
        for (int k = 0; k < 200; ++k) x.push_back(std::sin(0.05 * k) * 2.0);
        const std::vector<double> y = modulation::pcmDecode(modulation::pcmEncode(x, 2.0, n), 2.0, n);
        double worst = 0;
        for (size_t k = 0; k < x.size(); ++k) worst = std::max(worst, std::fabs(x[k] - y[k]));
        check(y.size() == x.size() && worst <= 2.0 / (1 << n) + 1e-9, "PCM error n=" + std::to_string(n));
    }
    {
        std::vector<double> x;
        for (int k = 0; k < 400; ++k) x.push_back(std::sin(2 * modulation::kPi * k / 100.0));
        const std::vector<double> y = modulation::dmDecode(modulation::dmEncode(x, 0.08), 0.08);
        double worst = 0;
        for (size_t k = 20; k < x.size(); ++k) worst = std::max(worst, std::fabs(x[k] - y[k]));
        check(worst <= 2 * 0.08 + 1e-9, "DM tracking error");
    }

    std::filesystem::remove(tmp);
    std::cout << (failures ? RED : GREEN) << "Self-test: " << checks - failures << " / " << checks
              << " checks passed." << RESET << '\n';
    return failures == 0;
}

void enableAnsiColours() {
#ifdef _WIN32
    // Older Windows consoles need this flag before they understand ANSI colours.
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (GetConsoleMode(h, &mode)) SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
#endif
}

}  // namespace

int main(int argc, char* argv[]) {
    enableAnsiColours();
    std::filesystem::create_directories(kOutputDir);

    if (argc > 1 && std::string(argv[1]) == "--test") return selfTest() ? 0 : 1;

    std::cout << BOLD << CYAN
              << "==============================================\n"
                 "   Digital Signal Encoder / Decoder  (C++17)\n"
                 "==============================================" << RESET << '\n';

    const std::vector<std::pair<std::string, std::function<void()>>> menu = {
        {"Digital input  -> line coding", digitalFlow},
        {"Analog input   -> PCM / DM -> line coding", analogFlow},
        {"Run self-test of all encoders and decoders", [] { selfTest(); }},
    };
    std::vector<std::string> names;
    for (const auto& item : menu) names.push_back(item.first);
    names.push_back("Exit");

    try {
        while (true) {
            const int k = choose("Main menu - what kind of input?", names);
            if (k == static_cast<int>(menu.size())) break;
            menu[k].second();
        }
    } catch (const InputClosed&) {
        std::cout << "\nInput closed.\n";
    } catch (const std::exception& e) {
        std::cout << RED << "Error: " << e.what() << RESET << '\n';
        return 1;
    }
    std::cout << "Goodbye.\n";
    return 0;
}
