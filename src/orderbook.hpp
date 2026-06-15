#pragma once
#include <map>
#include <string>
#include "order.hpp"
#include "ring_buffer.hpp"
#include "orderbook_base.hpp"

// LimitOrderBook — std::map price levels, RingBuffer<Order,64> per level.
//
// std::map gives O(log N) insert and O(1) best-bid/ask (begin()).
// RingBuffer replaces std::list — contiguous memory, zero heap allocation
// per order, cache-friendly iteration. See ring_buffer.hpp for design notes.
//
// orderIndex and cancelOrder() inherited from OrderBookBase.

class LimitOrderBook : public OrderBookBase {
public:
    static constexpr size_t LEVEL_CAPACITY = 64;
    using OrderQueue = RingBuffer<Order, LEVEL_CAPACITY>;

    // bids: highest price first (std::greater) — begin() = best bid
    std::map<double, OrderQueue, std::greater<double>> bids;
    // asks: lowest price first (default) — begin() = best ask
    std::map<double, OrderQueue> asks;

    void addOrder(const Order& order) {
        if (order.side == Side::BUY) {
            auto& q = bids[order.price];
            q.push_back(order);
            orderIndex[order.id] = q.back_ptr();
        } else {
            auto& q = asks[order.price];
            q.push_back(order);
            orderIndex[order.id] = q.back_ptr();
        }
    }
};