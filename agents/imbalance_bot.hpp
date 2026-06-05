#pragma once
#include "bot.hpp"
#include "../src/orderbook.hpp"
#include "../src/flat_orderbook.hpp"
#include "../src/order.hpp"
#include "../src/rdtsc.hpp"
#include "regime_detector.hpp"

class ImbalanceBot : public Bot {
    double threshold_;
    RegimeDetector regime_;

    int    fillCount_        = 0;
    int    adverseFillCount_ = 0;
    double lastFillPrice_    = 0.0;
    bool   lastWasBuy_       = false;
    static constexpr double TOXICITY_LIMIT = 0.60;

public:
    ImbalanceBot(std::string name, double threshold = 0.25)
        : Bot(std::move(name)), threshold_(threshold) {}

    void onPriceUpdate(double price, LimitOrderBook& lob, int) override {
        regime_.update(price);
        checkAdverse(price);
        if (regime_.isMeanRevert()) return;
        double imb = lob.imbalance(5);
        trade(price, imb, lob);
    }

    void onPriceUpdate(double price, FlatOrderBook&, int) override {
        regime_.update(price);
    }

    double toxicityScore() const {
        return fillCount_ == 0 ? 0.0 : static_cast<double>(adverseFillCount_) / fillCount_;
    }

private:
    void checkAdverse(double currentPrice) {
        if (lastFillPrice_ == 0.0) return;
        bool adverse = lastWasBuy_ ? (currentPrice < lastFillPrice_)
                                   : (currentPrice > lastFillPrice_);
        if (adverse) adverseFillCount_++;
    }

    template<typename Book>
    void trade(double price, double imbalance, Book& lob) {
        if (!riskCheck(price)) return;
        if (toxicityScore() > TOXICITY_LIMIT) return;

        if (imbalance > threshold_) {
            auto ord = Order::makeLimitOrder(name, price - 0.05, 1, Side::BUY);
            ord.tsc_created = rdtsc();
            lob.addOrder(ord);
            lastFillPrice_ = price; lastWasBuy_ = true; fillCount_++;
        } else if (imbalance < -threshold_) {
            auto ord = Order::makeLimitOrder(name, price + 0.05, 1, Side::SELL);
            ord.tsc_created = rdtsc();
            lob.addOrder(ord);
            lastFillPrice_ = price; lastWasBuy_ = false; fillCount_++;
        }
    }
};