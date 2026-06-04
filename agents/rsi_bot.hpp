#pragma once
#include <deque>
#include "bot.hpp"
#include "../src/orderbook.hpp"
#include "../src/flat_orderbook.hpp"
#include "../src/rdtsc.hpp"
#include "regime_detector.hpp"

class RSIBot : public Bot {
    int    period_;
    double oversold_, overbought_;
    std::deque<double> prices_;
    RegimeDetector regime_;

public:
    RSIBot(std::string name, int period = 14,
           double oversold = 30.0, double overbought = 70.0)
        : Bot(std::move(name)), period_(period),
          oversold_(oversold), overbought_(overbought) {}

    void onPriceUpdate(double price, LimitOrderBook& lob, int) override { place(price, lob); }
    void onPriceUpdate(double price, FlatOrderBook&  lob, int) override { place(price, lob); }

private:
    double computeRSI() const {
        double gain = 0.0, loss = 0.0;
        for (int i = 1; i <= period_; ++i) {
            double d = prices_[i] - prices_[i - 1];
            if (d > 0) gain += d; else loss -= d;
        }
        gain /= period_; loss /= period_;
        if (loss == 0.0) return 100.0;
        return 100.0 - (100.0 / (1.0 + gain / loss));
    }

    template<typename Book>
    void place(double price, Book& lob) {
        regime_.update(price);
        prices_.push_back(price);
        if ((int)prices_.size() > period_ + 1) prices_.pop_front();
        if ((int)prices_.size() < period_ + 1) return;
        if (regime_.isTrending()) return;

        double rsi = computeRSI();
        if (rsi < oversold_) {
            auto ord = Order::makeLimitOrder(name, price - 0.05, 1, Side::BUY);
            ord.tsc_created = rdtsc();
            lob.addOrder(ord);
        } else if (rsi > overbought_) {
            auto ord = Order::makeLimitOrder(name, price + 0.05, 1, Side::SELL);
            ord.tsc_created = rdtsc();
            lob.addOrder(ord);
        }
    }
};