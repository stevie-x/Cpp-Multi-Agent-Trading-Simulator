#pragma once
#include "bot.hpp"
#include <random>
#include "../src/orderbook.hpp"
#include "../src/flat_orderbook.hpp"
#include "../src/rdtsc.hpp"

class RandomBot : public Bot {
    std::mt19937 rng_;
    std::uniform_int_distribution<int> dist_{0, 9};

public:
    RandomBot(std::string n) : Bot(std::move(n)), rng_(std::random_device{}()) {}

    void onPriceUpdate(double price, LimitOrderBook& lob, int) override { place(price, lob); }
    void onPriceUpdate(double price, FlatOrderBook&  lob, int) override { place(price, lob); }

private:
    template<typename Book>
    void place(double price, Book& lob) {
        int r = dist_(rng_);
        if (r < 3) {
            auto ord = Order::makeLimitOrder(name, price - 0.10, 1, Side::BUY);
            ord.tsc_created = rdtsc();
            lob.addOrder(ord);
        } else if (r > 7) {
            auto ord = Order::makeLimitOrder(name, price + 0.10, 1, Side::SELL);
            ord.tsc_created = rdtsc();
            lob.addOrder(ord);
        }
    }
};