#pragma once
#include <deque>
#include "bot.hpp"
#include "../src/order.hpp"
#include "../src/orderbook.hpp"
#include "../src/flat_orderbook.hpp"
#include "../src/rdtsc.hpp"
#include "regime_detector.hpp"

class MomentumBot : public Bot {
    std::deque<double> prices_;
    int window_;
    RegimeDetector regime_;

public:
    MomentumBot(std::string name, int window = 5)
        : Bot(std::move(name)), window_(window) {}

    void onPriceUpdate(double price, LimitOrderBook& lob, int) override { place(price, lob); }
    void onPriceUpdate(double price, FlatOrderBook&  lob, int) override { place(price, lob); }

private:
    double halfAvg(bool recent) const {
        int half = window_ / 2;
        double sum = 0.0;
        if (recent) {
            auto it = prices_.end();
            for (int i = 0; i < half; ++i) { --it; sum += *it; }
        } else {
            auto it = prices_.begin();
            for (int i = 0; i < half; ++i, ++it) sum += *it;
        }
        return sum / half;
    }

    template<typename Book>
    void place(double price, Book& lob) {
        regime_.update(price);
        prices_.push_back(price);
        if ((int)prices_.size() > window_) prices_.pop_front();
        if ((int)prices_.size() < window_) return;
        if (!regime_.isTrending() && regime_.currentRegime != Regime::UNDEFINED) return;

        if (halfAvg(true) > halfAvg(false)) {
            auto ord = Order::makeLimitOrder(name, price - 0.05, 1, Side::BUY);
            ord.tsc_created = rdtsc();
            lob.addOrder(ord);
        } else if (halfAvg(true) < halfAvg(false)) {
            auto ord = Order::makeLimitOrder(name, price + 0.05, 1, Side::SELL);
            ord.tsc_created = rdtsc();
            lob.addOrder(ord);
        }
    }
};