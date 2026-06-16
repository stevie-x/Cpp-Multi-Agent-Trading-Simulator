#pragma once
#include <deque>
#include <cassert>
#include <string>
#include "../src/order.hpp"
#include "../src/orderbook.hpp"
#include "../src/flat_orderbook.hpp"
#include "bot.hpp"

// MomentumBot — places orders based on short-term price momentum.
//
// Signal: if the average of the most recent (window/2) prices is higher than
// the average of the earliest (window/2) prices → uptrend → buy.
// Opposite → downtrend → sell.
//
// FIX 14: constructor now asserts window >= 2 and window is even.
//   Old: window=5 silently ignored the middle price (integer division drops it).
//        window=1 caused div-by-zero in recentAvg()/earlierAvg().
//   New: assert fires immediately at construction with a clear message.
//
// FIX 16: order offset is now proportional to price (5 basis points = 0.05%).
//   Old: fixed $1 offset — at BTC $65,000 this is 0.0015%, effectively a
//        market order. Behaviour was completely different across instruments.
//   New: offset = price * 0.0005 — same relative spread on ETH and BTC.
//
// FIX 17: position limits enforced. Bot will not accumulate beyond maxPosition.

class MomentumBot : public Bot {
    std::deque<double> prices;
    int window;

public:
    // window must be >= 2 and even. Default 6 (was 5 — odd, caused data loss).
    // maxPosition: maximum long/short position size.
    explicit MomentumBot(std::string name, int window = 6, int maxPosition = 10)
        : Bot(name, maxPosition), window(window)
    {
        // FIX 14: catch bad window values at construction, not silently at runtime
        assert(window >= 2 && "MomentumBot: window must be >= 2");
        assert(window % 2 == 0 && "MomentumBot: window must be even — odd windows drop the middle price via integer division");
    }

    void onPriceUpdate(double price, LimitOrderBook& lob, int timestep) override;
    void onPriceUpdate(double price, FlatOrderBook&  lob, int timestep) override;

private:
    double recentAvg()  const;
    double earlierAvg() const;
    bool shouldBuy()    const;
    bool shouldSell()   const;

    template<typename Book>
    void placeOrders(double price, Book& lob) {
        prices.push_back(price);
        if ((int)prices.size() > window) prices.pop_front();
        if ((int)prices.size() < window) return;

        // FIX 16: proportional offset — 5 basis points (0.05% of current price).
        // At ETH $2400: offset = $1.20  (was fixed $1.00 — close but inconsistent)
        // At BTC $65000: offset = $32.50 (was fixed $1.00 — essentially market order)
        const double offset = price * 0.0005;

        if (shouldBuy() && position < maxPosition)
            lob.addOrder(Order::makeLimitOrder(name, price - offset, 1, Side::BUY));
        else if (shouldSell() && position > -maxPosition)
            lob.addOrder(Order::makeLimitOrder(name, price + offset, 1, Side::SELL));
    }
};