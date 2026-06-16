#pragma once
#include <deque>
#include "bot.hpp"
#include "../src/orderbook.hpp"
#include "../src/flat_orderbook.hpp"

// RSIBot — trades on Wilder's Relative Strength Index.
//
// FIX 15: RSI now uses Wilder's exponential smoothing, not a simple average.
//
//   Old (simple average — wrong):
//     avgGain = sum(gains over period) / period   ← recomputed from scratch each tick
//     avgLoss = sum(losses over period) / period
//     RSI was noisier and not the actual RSI formula.
//
//   New (Wilder's smoothed — correct):
//     First RSI: simple average over first `period` changes (seeding)
//     Subsequent:
//       avgGain = (prevAvgGain * (period - 1) + currentGain) / period
//       avgLoss = (prevAvgLoss * (period - 1) + currentLoss) / period
//     This gives the characteristic smoothed RSI used in real trading systems.
//     The exponential weighting means older prices decay in influence — the
//     signal stabilises rather than jumping around on every tick.
//
// FIX 16: order offset is price-proportional (5 bps) not fixed $1.
// FIX 17: position limits enforced via Bot::maxPosition.

class RSIBot : public Bot {
    int    period;
    double oversold;
    double overbought;

    std::deque<double> prices;

    // Wilder's smoothed averages — carried forward between ticks.
    // Initialised to -1 to signal "not yet seeded".
    double prevAvgGain = -1.0;
    double prevAvgLoss = -1.0;

public:
    RSIBot(std::string name,
           int    period     = 14,
           double oversold   = 30.0,
           double overbought = 70.0,
           int    maxPos     = 10)
        : Bot(name, maxPos)
        , period(period)
        , oversold(oversold)
        , overbought(overbought)
    {}

    void onPriceUpdate(double price, LimitOrderBook& lob, int) override { placeOrders(price, lob); }
    void onPriceUpdate(double price, FlatOrderBook&  lob, int) override { placeOrders(price, lob); }

private:
    // computeRSI — Wilder's smoothed RSI.
    // Returns -1.0 if not enough data yet (< period + 1 prices).
    double computeRSI() {
        if ((int)prices.size() < period + 1) return -1.0;

        // Seed phase: first RSI uses simple average over first `period` changes.
        if (prevAvgGain < 0.0) {
            double gainSum = 0.0, lossSum = 0.0;
            for (int i = 1; i <= period; ++i) {
                double change = prices[i] - prices[i - 1];
                if (change > 0) gainSum += change;
                else            lossSum -= change;
            }
            prevAvgGain = gainSum / period;
            prevAvgLoss = lossSum / period;
        } else {
            // Wilder's smoothing: exponential decay with alpha = 1/period.
            // New change is the most recent price transition only.
            double change = prices.back() - prices[prices.size() - 2];
            double gain   = (change > 0) ? change : 0.0;
            double loss   = (change < 0) ? -change : 0.0;

            prevAvgGain = (prevAvgGain * (period - 1) + gain) / period;
            prevAvgLoss = (prevAvgLoss * (period - 1) + loss) / period;
        }

        if (prevAvgLoss == 0.0) return 100.0;
        double rs = prevAvgGain / prevAvgLoss;
        return 100.0 - (100.0 / (1.0 + rs));
    }

    template<typename Book>
    void placeOrders(double price, Book& lob) {
        prices.push_back(price);
        // Keep only as many prices as needed for seeding + one update
        if ((int)prices.size() > period + 2) prices.pop_front();

        double rsi = computeRSI();
        if (rsi < 0.0) return;  // not enough data yet

        // FIX 16: proportional offset — 5 basis points
        const double offset = price * 0.0005;

        if (rsi < oversold && position < maxPosition)
            lob.addOrder(Order::makeLimitOrder(name, price - offset, 1, Side::BUY));
        else if (rsi > overbought && position > -maxPosition)
            lob.addOrder(Order::makeLimitOrder(name, price + offset, 1, Side::SELL));
    }
};