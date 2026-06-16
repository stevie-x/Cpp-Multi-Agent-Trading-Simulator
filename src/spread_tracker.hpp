#pragma once
#include <limits>
#include <cstdint>
#include <iostream>
#include <iomanip>
#include "orderbook.hpp"

// SpreadTracker — tracks bid-ask spread statistics across a simulation run.
//
// FIX 20: spread was never tracked or reported. Bid-ask spread is the most
// fundamental orderbook quality metric. A tight spread = liquid, efficient
// market. A wide spread = illiquid or poorly-implemented matching engine.
//
// Usage:
//   SpreadTracker st;
//   // after each tick where book has both sides:
//   st.record(lob);
//   // at end of simulation:
//   st.print("ETH");

class SpreadTracker {
public:
    void record(const LimitOrderBook& lob) {
        if (lob.bids.empty() || lob.asks.empty()) return;

        double bid    = lob.bids.begin()->first;
        double ask    = lob.asks.begin()->first;
        double spread = ask - bid;

        if (spread < 0.0) return;   // crossed book — skip (transient state)

        if (spread < minSpread_) minSpread_ = spread;
        if (spread > maxSpread_) maxSpread_ = spread;
        totalSpread_ += spread;
        count_++;
    }

    double minSpread() const { return count_ > 0 ? minSpread_ : 0.0; }
    double maxSpread() const { return count_ > 0 ? maxSpread_ : 0.0; }
    double avgSpread() const { return count_ > 0 ? totalSpread_ / count_ : 0.0; }
    uint64_t samples() const { return count_; }

    void print(const std::string& instrument) const {
        std::cout << std::fixed << std::setprecision(4);
        std::cout << "\n── Spread Analysis: " << instrument << " ────────────────────\n";
        if (count_ == 0) {
            std::cout << "  No spread data — book never had both sides simultaneously\n";
            return;
        }
        std::cout << "  Samples : " << count_  << " ticks with 2-sided book\n";
        std::cout << "  Min     : $" << minSpread() << "\n";
        std::cout << "  Avg     : $" << avgSpread() << "\n";
        std::cout << "  Max     : $" << maxSpread() << "\n";
        std::cout << "──────────────────────────────────────────────────────\n";
    }

private:
    double   minSpread_   = std::numeric_limits<double>::max();
    double   maxSpread_   = 0.0;
    double   totalSpread_ = 0.0;
    uint64_t count_       = 0;
};