#pragma once
#include <unordered_map>
#include "order.hpp"

// OrderBookBase — shared interface and orderIndex for all order book types.
//
// Both LimitOrderBook (std::map + RingBuffer) and FlatOrderBook
// (std::vector + RingBuffer) inherit from this base. Shared here:
//   - orderIndex: O(1) order lookup by ID for cancellation
//   - cancelOrder(): set quantity = 0 via pointer, erase from index
//
// Why quantity=0 rather than erasing the order from the price level?
// The price level queues (RingBuffer) are FIFO — we can only pop from
// the front. An order mid-queue can't be removed in O(1). Setting
// quantity=0 marks it as cancelled; the matching engine skips zero-qty
// orders when it reaches them at the front of the queue.
// This is the "lazy deletion" pattern used in real exchange matching engines.

class OrderBookBase {
public:
    std::unordered_map<int, Order*> orderIndex;

    // cancelOrder — O(1) cancellation via orderIndex pointer.
    // Returns true if the order was found and marked cancelled.
    // Returns false if the order ID is unknown (already filled, already
    // cancelled, or never existed).
    bool cancelOrder(int id) {
        auto it = orderIndex.find(id);
        if (it == orderIndex.end()) return false;
        it->second->quantity = 0;   // lazy deletion — matcher skips qty=0
        orderIndex.erase(it);
        return true;
    }

    virtual ~OrderBookBase() = default;
};