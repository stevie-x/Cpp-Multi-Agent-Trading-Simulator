#pragma once
#include <map>
#include <string>
#include <unordered_map>
#include "order.hpp"
#include "ring_buffer.hpp"

// LimitOrderBook — RingBuffer<Order, 64> replaces std::list<Order>
//
// BEFORE: std::list — heap node per order, cache-unfriendly
// AFTER:  RingBuffer — 64 slots inline in the map value, contiguous memory,
//         zero heap allocation per order, cache-friendly iteration

class LimitOrderBook {
public:
    static constexpr size_t LEVEL_CAPACITY = 64;
    using OrderQueue = RingBuffer<Order, LEVEL_CAPACITY>;

    std::map<double, OrderQueue, std::greater<double>> bids;
    std::map<double, OrderQueue> asks;
    std::unordered_map<int, Order*> orderIndex;

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

    bool cancelOrder(int id) {
        auto it = orderIndex.find(id);
        if (it == orderIndex.end()) return false;
        it->second->quantity = 0;
        orderIndex.erase(it);
        return true;
    }
};