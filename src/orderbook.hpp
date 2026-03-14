#pragma once
#include <map>
#include <queue>
#include <string>
#include <iostream>
#include "order.hpp"   // ← use shared Order definition

class LimitOrderBook {
public:
    std::map<double, std::queue<Order>, std::greater<double>> bids;
    std::map<double, std::queue<Order>> asks;

    void addOrder(const Order& order, bool isBuy) {
        if(isBuy) bids[order.price].push(order);
        else asks[order.price].push(order);
    }
};