#pragma once
#include <deque>
#include <cmath>
#include "bot.hpp"
#include "../src/order.hpp"
#include "../src/orderbook.hpp"
#include "../src/flat_orderbook.hpp"
#include "../src/rdtsc.hpp"

class BollingerBot : public Bot {
    int    window_;
    double k_;
    std::deque<double> prices_;

public:
    BollingerBot(std::string name, int window = 20, double k = 2.0)
        : Bot(std::move(name)), window_(window), k_(k) {}

    void onPriceUpdate(double price, LimitOrderBook& lob, int ts) override { place(price, lob); }
    void onPriceUpdate(double price, FlatOrderBook&  lob, int ts) override { place(price, lob); }

private:
    struct Bands { double mid, upper, lower, sigma; };

    Bands compute() const {
        double sum = 0.0;
        for (double p : prices_) sum += p;
        double mean = sum / prices_.size();
        double var  = 0.0;
        for (double p : prices_) var += (p - mean) * (p - mean);
        double sigma = std::sqrt(var / prices_.size());
        return { mean, mean + k_ * sigma, mean - k_ * sigma, sigma };
    }

    template<typename Book>
    void place(double price, Book& lob) {
        if (!riskCheck(price)) return;
        prices_.push_back(price);
        if ((int)prices_.size() > window_) prices_.pop_front();
        if ((int)prices_.size() < window_) return;

        auto b = compute();
        double deviation = std::abs(price - b.mid) / (b.sigma + 1e-9);

        if (price < b.lower) {
            double limit = (deviation > 2.5) ? price + 0.01 : price - 0.05;
            auto ord = Order::makeLimitOrder(name, limit, 1, Side::BUY);
            ord.tsc_created = rdtsc();
            lob.addOrder(ord);
        } else if (price > b.upper) {
            double limit = (deviation > 2.5) ? price - 0.01 : price + 0.05;
            auto ord = Order::makeLimitOrder(name, limit, 1, Side::SELL);
            ord.tsc_created = rdtsc();
            lob.addOrder(ord);
        }
    }
};