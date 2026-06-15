#pragma once
#include <vector>
#include <algorithm>
#include "order.hpp"
#include "ring_buffer.hpp"
#include "orderbook_base.hpp"

// FlatOrderBook — sorted std::vector of price levels, RingBuffer per level.
//
// vs LimitOrderBook (std::map):
//   std::map = red-black tree, each node is a separate heap allocation.
//   Every insert/lookup chases pointers across memory → cache misses.
//   std::vector = contiguous memory, all levels in one allocation.
//   CPU prefetcher works effectively; binary search gives O(log N) with
//   much lower constants at realistic price-level counts (<200 levels).
//
// RingBuffer replaces std::deque — see ring_buffer.hpp for design notes.
// IMPORTANT: std::deque::push_back() can invalidate pointers stored in
// orderIndex. RingBuffer nodes never move after insertion — safe for
// pointer storage.
//
// asks stored in ASCENDING order (front = best ask, lowest price).
// bids stored in DESCENDING order (back = best bid, highest price).
// Both use pop_back() for O(1) best-level removal.
//
// orderIndex and cancelOrder() inherited from OrderBookBase.

struct PriceLevel {
    double                    price;
    RingBuffer<Order, 64>     orders;
};

class FlatOrderBook : public OrderBookBase {
public:
    // asks: ascending — front() = lowest ask = best ask
    std::vector<PriceLevel> asks;
    // bids: descending — back() = highest bid = best bid
    std::vector<PriceLevel> bids;

    void addOrder(const Order& order) {
        if (order.side == Side::BUY) {
            auto& level = findOrInsertBid(order.price);
            level.orders.push_back(order);
            orderIndex[order.id] = level.orders.back_ptr();
        } else {
            auto& level = findOrInsertAsk(order.price);
            level.orders.push_back(order);
            orderIndex[order.id] = level.orders.back_ptr();
        }
    }

    bool bidsEmpty() const { return bids.empty(); }
    bool asksEmpty() const { return asks.empty(); }

    // Best bid = highest price = back of descending bids vector
    PriceLevel& bestBid() { return bids.back(); }
    // Best ask = lowest price = front of ascending asks vector
    PriceLevel& bestAsk() { return asks.front(); }

    // O(1) removal — pop_back() on a vector is O(1), no element shifting
    void removeBestBid() { bids.pop_back(); }

    // O(1) removal — swap front with back then pop_back.
    // We can do this because after the best ask is exhausted there are no
    // resting orders at that price — order of remaining levels doesn't change
    // relative to each other.
    void removeBestAsk() {
        if (asks.size() == 1) { asks.pop_back(); return; }
        asks.front() = std::move(asks.back());
        asks.pop_back();
        // Re-sort after swap to maintain ascending order
        std::sort(asks.begin(), asks.end(),
            [](const PriceLevel& a, const PriceLevel& b){ return a.price < b.price; });
    }

private:
    // Find existing level or insert new one, maintaining ascending order.
    PriceLevel& findOrInsertAsk(double price) {
        auto it = std::lower_bound(asks.begin(), asks.end(), price,
            [](const PriceLevel& lvl, double p){ return lvl.price < p; });
        if (it != asks.end() && it->price == price) return *it;
        it = asks.insert(it, PriceLevel{price, {}});
        return *it;
    }

    // Find existing level or insert new one, maintaining descending order.
    PriceLevel& findOrInsertBid(double price) {
        auto it = std::lower_bound(bids.begin(), bids.end(), price,
            [](const PriceLevel& lvl, double p){ return lvl.price > p; });
        if (it != bids.end() && it->price == price) return *it;
        it = bids.insert(it, PriceLevel{price, {}});
        return *it;
    }
};