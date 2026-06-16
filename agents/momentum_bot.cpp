#include "momentum_bot.hpp"

// recentAvg — average of the most recent (window/2) prices.
// Window is guaranteed even by constructor assert, so half = window/2
// has no rounding loss and no prices are skipped.
double MomentumBot::recentAvg() const {
    int half = window / 2;
    double sum = 0.0;
    auto it = prices.end();
    for (int i = 0; i < half; ++i) { --it; sum += *it; }
    return sum / half;
}

// earlierAvg — average of the oldest (window/2) prices.
double MomentumBot::earlierAvg() const {
    int half = window / 2;
    double sum = 0.0;
    auto it = prices.begin();
    for (int i = 0; i < half; ++i, ++it) sum += *it;
    return sum / half;
}

bool MomentumBot::shouldBuy()  const { return recentAvg() > earlierAvg(); }
bool MomentumBot::shouldSell() const { return recentAvg() < earlierAvg(); }

void MomentumBot::onPriceUpdate(double price, LimitOrderBook& lob, int) {
    placeOrders(price, lob);
}

void MomentumBot::onPriceUpdate(double price, FlatOrderBook& lob, int) {
    placeOrders(price, lob);
}