// =============================================================================
// plot.hpp
// -----------------------------------------------------------------------------
// Graphical output, with NO external library:
//
//   1. ASCII plots in the terminal (instant feedback while you use the menu)
//   2. SVG plots collected into one HTML report (output/report.html).
//      SVG (Scalable Vector Graphics) is just text describing lines and shapes,
//      so C++ can write it with ordinary string streams, and any web browser
//      draws it.
// =============================================================================
#pragma once

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

#include "line_coding.hpp"

namespace plot {

// ------------------------------ ASCII: digital -------------------------------
// 5 text rows: +V, (gap), 0, (gap), -V. The gaps hold the '|' of vertical edges.
// Each half-bit is 3 characters wide, so one bit is 6 characters wide.
inline void asciiDigital(std::ostream& os, const Bits& bits, const EncodedSignal& e,
                         int bitsPerRow = 16) {
    const int W = 3;
    const char* labels[5] = {"+V |", "   |", " 0 |", "   |", "-V |"};
    auto rowOf = [](int level) { return 2 - 2 * level; };  // +1 -> 0, 0 -> 2, -1 -> 4
    const int n = static_cast<int>(bits.size());

    for (int start = 0; start < n; start += bitsPerRow) {
        const int end = std::min(n, start + bitsPerRow);
        const int width = (end - start) * 2 * W;

        // Bit values above the waveform, plus bit index of the first bit.
        std::string header(width, ' ');
        for (int b = start; b < end; ++b) header[(b - start) * 2 * W + W - 1] = bits[b];
        os << "    " << header << "   (bits " << start << ".." << end - 1 << ")\n";

        std::vector<std::string> grid(5, std::string(width, ' '));
        grid[2] = std::string(width, '.');  // the 0 V axis

        int previous = e.halfLevels[start == 0 ? 0 : 2 * start - 1];
        for (int h = 0; h < 2 * (end - start); ++h) {
            const int level = e.halfLevels[2 * start + h];
            const int row = rowOf(level), col = h * W;
            for (int c = col; c < col + W; ++c) grid[row][c] = '_';
            if (level != previous) {  // draw the vertical edge
                const int oldRow = rowOf(previous);
                for (int r = std::min(oldRow, row) + 1; r < std::max(oldRow, row); ++r)
                    grid[r][col] = '|';
                grid[oldRow][col] = '+';
                grid[row][col] = '+';
            }
            previous = level;
        }
        for (int r = 0; r < 5; ++r) os << labels[r] << grid[r] << '\n';

        // Mark bits replaced by a scrambler with ^^^.
        std::string marks(width, ' ');
        bool any = false;
        for (int b = start; b < end; ++b)
            if (e.substituted[b]) {
                any = true;
                std::fill(marks.begin() + (b - start) * 2 * W,
                          marks.begin() + (b - start + 1) * 2 * W, '^');
            }
        if (any) os << "scr " << marks << "  (^ = substituted)\n";
        os << '\n';
    }
}

// ------------------------------ ASCII: analog --------------------------------
// '*' = analog sample, '#' = decoder staircase, '@' = both in the same cell.
inline void asciiAnalog(std::ostream& os, const std::vector<double>& x,
                        const std::vector<double>& staircase, int height = 15, int maxCols = 72) {
    const int n = static_cast<int>(x.size());
    if (n == 0) return;
    double top = 1e-9;
    for (double v : x) top = std::max(top, std::fabs(v));
    for (double v : staircase) top = std::max(top, std::fabs(v));
    top *= 1.05;

    const int cols = std::min(n, maxCols);
    std::vector<std::string> grid(height, std::string(cols, ' '));
    grid[height / 2] = std::string(cols, '-');  // 0 axis
    auto rowOf = [&](double v) {
        return std::clamp(static_cast<int>(std::lround((top - v) / (2 * top) * (height - 1))), 0,
                          height - 1);
    };
    for (int c = 0; c < cols; ++c) {
        const int k = static_cast<int>(static_cast<long long>(c) * n / cols);
        grid[rowOf(x[k])][c] = '*';
        if (k < static_cast<int>(staircase.size())) {
            char& cell = grid[rowOf(staircase[k])][c];
            cell = (cell == '*') ? '@' : '#';
        }
    }
    os << std::fixed << std::setprecision(2);
    for (int r = 0; r < height; ++r) {
        const double value = top - 2 * top * r / (height - 1);
        os << std::setw(7) << value << " |" << grid[r] << '\n';
    }
    os << "         * analog samples   # decoder staircase   @ both\n\n";
}

// ------------------------------- SVG helpers ---------------------------------
inline std::string num(double v) {
    std::ostringstream s;
    s << std::fixed << std::setprecision(2) << v;
    return s.str();
}

// Digital waveform as SVG. Scrambled bits get a yellow background.
inline std::string svgDigital(const Bits& bits, const EncodedSignal& e) {
    const double bitW = 36, left = 44, height = 150;
    const double width = left + bits.size() * bitW + 10;
    auto y = [](int level) { return 75.0 - 40.0 * level; };  // +V -> 35, 0 -> 75, -V -> 115

    std::ostringstream s;
    s << "<svg xmlns='http://www.w3.org/2000/svg' width='" << num(width) << "' height='" << height
      << "'>\n";
    for (size_t b = 0; b < bits.size(); ++b) {
        const double x0 = left + b * bitW;
        if (e.substituted[b])
            s << "<rect x='" << num(x0) << "' y='25' width='" << bitW
              << "' height='100' fill='#fde68a'/>\n";
        s << "<line x1='" << num(x0) << "' y1='25' x2='" << num(x0)
          << "' y2='125' stroke='#cbd5e1' stroke-dasharray='3,3'/>\n";
        s << "<text x='" << num(x0 + bitW / 2) << "' y='16' text-anchor='middle'>" << bits[b]
          << "</text>\n";
    }
    s << "<line x1='" << left << "' y1='75' x2='" << num(width - 10)
      << "' y2='75' stroke='#94a3b8'/>\n";
    s << "<text x='4' y='39'>+V</text><text x='10' y='79'>0</text><text x='4' y='119'>-V</text>\n";

    // The waveform itself: one path of horizontal and vertical moves.
    const double halfW = bitW / 2;
    s << "<path fill='none' stroke='#2563eb' stroke-width='2.5' d='M " << left << ' '
      << num(y(e.halfLevels[0]));
    for (size_t k = 0; k < e.halfLevels.size(); ++k) {
        s << " H " << num(left + (k + 1) * halfW);
        if (k + 1 < e.halfLevels.size() && e.halfLevels[k + 1] != e.halfLevels[k])
            s << " V " << num(y(e.halfLevels[k + 1]));
    }
    s << "'/>\n</svg>\n";
    return s.str();
}

// Analog signal (dense curve), its samples (dots) and the staircase (red steps).
inline std::string svgAnalog(const std::vector<double>& denseT, const std::vector<double>& denseX,
                             const std::vector<double>& sampleT, const std::vector<double>& sampleX,
                             const std::vector<double>& staircase, double duration) {
    const double W = 900, H = 300, left = 50, right = 15, top = 15, bottom = 30;
    double peak = 1e-9;
    for (double v : denseX) peak = std::max(peak, std::fabs(v));
    for (double v : staircase) peak = std::max(peak, std::fabs(v));
    peak *= 1.1;
    auto px = [&](double t) { return left + t / duration * (W - left - right); };
    auto py = [&](double v) { return top + (peak - v) / (2 * peak) * (H - top - bottom); };

    std::ostringstream s;
    s << "<svg xmlns='http://www.w3.org/2000/svg' width='" << W << "' height='" << H << "'>\n";
    s << "<line x1='" << left << "' y1='" << num(py(0)) << "' x2='" << W - right << "' y2='"
      << num(py(0)) << "' stroke='#94a3b8'/>\n";
    s << "<text x='4' y='" << num(py(peak / 1.1) + 4) << "'>" << num(peak / 1.1) << "</text>";
    s << "<text x='4' y='" << num(py(-peak / 1.1) + 4) << "'>" << num(-peak / 1.1) << "</text>\n";
    s << "<text x='" << W - right << "' y='" << H - 8 << "' text-anchor='end'>t = "
      << num(duration * 1000) << " ms</text>\n";

    s << "<polyline fill='none' stroke='#2563eb' stroke-width='2' points='";
    for (size_t i = 0; i < denseT.size(); ++i) s << num(px(denseT[i])) << ',' << num(py(denseX[i])) << ' ';
    s << "'/>\n";

    // Staircase: hold each decoded value until the next sample instant.
    if (!staircase.empty()) {
        s << "<path fill='none' stroke='#dc2626' stroke-width='2' d='M " << num(px(sampleT[0]))
          << ' ' << num(py(staircase[0]));
        for (size_t k = 0; k < staircase.size(); ++k) {
            const double tEnd = (k + 1 < sampleT.size()) ? sampleT[k + 1] : duration;
            s << " H " << num(px(tEnd));
            if (k + 1 < staircase.size()) s << " V " << num(py(staircase[k + 1]));
        }
        s << "'/>\n";
    }
    for (size_t k = 0; k < sampleT.size(); ++k)
        s << "<circle cx='" << num(px(sampleT[k])) << "' cy='" << num(py(sampleX[k]))
          << "' r='2.5' fill='#0f172a'/>\n";
    s << "</svg>\n";
    return s.str();
}

// ------------------------------- HTML report ---------------------------------
class Report {
public:
    void add(const std::string& heading, const std::string& html) {
        body_ << "<section><h2>" << heading << "</h2>\n" << html << "</section>\n";
    }
    void addText(const std::string& heading, const std::string& text) {
        add(heading, "<pre>" + text + "</pre>");
    }
    void write(const std::string& path) const {
        std::ofstream f(path);
        f << "<!doctype html><html><head><meta charset='utf-8'><title>Line Coding Report</title>"
             "<style>body{font-family:Segoe UI,Arial,sans-serif;margin:24px;background:#f8fafc;"
             "color:#0f172a}section{background:#fff;border:1px solid #e2e8f0;border-radius:8px;"
             "padding:12px 16px;margin-bottom:16px}h2{font-size:17px;margin:4px 0 10px}"
             ".scroll{overflow-x:auto}pre{white-space:pre-wrap;word-break:break-all;"
             "font-size:13px}svg text{font:12px monospace;fill:#334155}</style></head><body>"
             "<h1>Digital Signal Encoder / Decoder - Report</h1>\n"
          << body_.str() << "</body></html>\n";
    }

private:
    std::ostringstream body_;
};

}  // namespace plot
