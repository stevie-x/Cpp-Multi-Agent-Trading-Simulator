#pragma once
#include "bot.hpp"
#include <random>
#include "../src/orderbook.hpp"
#include "../src/flat_orderbook.hpp"

// RandomBot — noise trader with configurable probabilities.
//
// FIX 13: sell threshold changed from r > 7 (20%) to r >= 7 (30%).
//   Old: buy=30% sell=20% hold=50%  ← buy-side bias, doesn't match README
//   New: buy=30% sell=30% hold=40%  ← matches documented 30/30/40 split
//
// FIX 17: position limits added. Bot will not place a buy if position
//   >= maxPosition, and will not place a sell if position <= -maxPosition.
//   Without limits, a bot in a trending market accumulates infinite position
//   and P&L numbers become meaningless.

class RandomBot : public Bot {
    std::mt19937 rng;
    std::uniform_int_distribution<int> dist{0, 9};

public:
    // maxPosition: maximum number of units held long or short at any time.
    // Default 10 — large enough to trade freely, small enough to be realistic.
    explicit RandomBot(std::string n, int maxPosition = 10)
        : Bot(n, maxPosition), rng(std::random_device{}()) {}

    void onPriceUpdate(double price, LimitOrderBook& lob, int) override { placeOrders(price, lob); }
    void onPriceUpdate(double price, FlatOrderBook&  lob, int) override { placeOrders(price, lob); }

private:
    template<typename Book>
    void placeOrders(double price, Book& lob) {
        int r = dist(rng);

        if (r < 3) {
            // Buy 30% of the time — only if under position limit
            if (position < maxPosition)
                lob.addOrder(Order::makeLimitOrder(name, price - 5.0, 1, Side::BUY));
        } else if (r >= 7) {
            // FIX: was r > 7 (values 8,9 = 20%). Now r >= 7 (values 7,8,9 = 30%).
            // Sell 30% of the time — only if not at short limit
            if (position > -maxPosition)
                lob.addOrder(Order::makeLimitOrder(name, price + 5.0, 1, Side::SELL));
        }
        // else hold — 40% of the time (values 3,4,5,6)
    }
};