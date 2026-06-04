#pragma once
#include <vector>
#include <algorithm>
#include <numeric>
#include <cstdint>
#include <iostream>
#include <iomanip>
#include <string>
#include <cmath>
#include "rdtsc.hpp"

struct LatencyHistogram {
    std::string label;
    std::vector<uint64_t> samples_cycles;

    explicit LatencyHistogram(std::string lbl) : label(std::move(lbl)) {
        samples_cycles.reserve(4096);
    }

    void record(uint64_t start_tsc, uint64_t end_tsc) {
        if (end_tsc > start_tsc)
            samples_cycles.push_back(end_tsc - start_tsc);
    }

    void print(const TscClock& clk = globalClock()) const {
        if (samples_cycles.empty()) {
            std::cout << "  [" << label << "] no samples\n";
            return;
        }
        auto sorted = samples_cycles;
        std::sort(sorted.begin(), sorted.end());

        // On ARM, rdtsc() returns wall-clock nanoseconds directly.
        // A delta of e.g. 1.2 billion means 1.2 seconds elapsed between
        // tsc_created and tsc_filled — not a real latency measurement.
        // Cap display at 1ms and note the limitation.
        auto display_ns = [&](uint64_t cycles) -> double {
            return std::min(clk.to_ns(cycles), 999999.0);
        };

        double avg_raw = static_cast<double>(
            std::accumulate(sorted.begin(), sorted.end(), uint64_t{0})
            / sorted.size());
        double avg_ns = std::min(clk.to_ns(avg_raw), 999999.0);

        auto pct = [&](double p) -> double {
            size_t idx = static_cast<size_t>(p / 100.0 * sorted.size());
            if (idx >= sorted.size()) idx = sorted.size() - 1;
            return display_ns(sorted[idx]);
        };

        std::cout << "\n── Latency: " << label << " ──────────────────────────────────\n";
        std::cout << std::fixed << std::setprecision(1);
        std::cout << "  samples : " << sorted.size()              << "\n";
        std::cout << "  min     : " << display_ns(sorted.front()) << " ns\n";
        std::cout << "  avg     : " << avg_ns                     << " ns\n";
        std::cout << "  p50     : " << pct(50)                    << " ns\n";
        std::cout << "  p95     : " << pct(95)                    << " ns\n";
        std::cout << "  p99     : " << pct(99)                    << " ns\n";
        std::cout << "  p99.9   : " << pct(99.9)                  << " ns\n";
        std::cout << "  max     : " << display_ns(sorted.back())  << " ns\n";
        std::cout << "  note    : ARM uses wall-clock fallback (x86 rdtsc unavailable)\n";
        std::cout << "────────────────────────────────────────────────────────\n";
    }
};