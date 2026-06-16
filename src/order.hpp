#pragma once
#include <atomic>
#include <limits>
#include <string>

// enum class prevents accidental implicit conversion to int (safer than plain enum)
enum class Side { BUY, SELL };

struct Order {
    int         id;
    std::string agent;
    double      price;
    int         quantity;
    Side        side;

    // FIX 18: nextId is now a per-book counter, not a global static.
    // Old: inline static std::atomic<int> nextId{0} — shared across ETH and BTC.
    //   ETH got IDs 0,2,4 and BTC got 1,3,5 — interleaved, confusing in logs.
    //   Real exchanges have per-instrument monotonic sequence numbers.
    // New: callers pass their own counter into makeOrder() functions.
    //   Each LimitOrderBook or FlatOrderBook owns an atomic<int> seqNum{0}
    //   and passes it here. IDs restart at 0 per book, monotonic per instrument.
    //
    // For backwards compatibility with code that doesn't pass a counter yet,
    // a global fallback counter is still provided but marked deprecated.

    static Order makeLimitOrder(std::string agent, double price,
                                int quantity, Side side,
                                std::atomic<int>& counter) {
        return { counter++, std::move(agent), price, quantity, side };
    }

    static Order makeMarketOrder(std::string agent, int quantity,
                                 Side side, std::atomic<int>& counter) {
        double p = (side == Side::BUY) ? std::numeric_limits<double>::max() : 0.0;
        return { counter++, std::move(agent), p, quantity, side };
    }

    // Backwards-compatible overloads using the global counter.
    // These allow existing call sites to compile without changes.
    // Migrate call sites to the counter-passing versions over time.
    static std::atomic<int>& globalCounter() {
        static std::atomic<int> c{0};
        return c;
    }

    static Order makeLimitOrder(std::string agent, double price,
                                int quantity, Side side) {
        return makeLimitOrder(std::move(agent), price, quantity, side, globalCounter());
    }

    static Order makeMarketOrder(std::string agent, int quantity, Side side) {
        return makeMarketOrder(std::move(agent), quantity, side, globalCounter());
    }
};