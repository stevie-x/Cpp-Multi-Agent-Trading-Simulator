#pragma once
#include <string>
#include <algorithm>

class LimitOrderBook;
class FlatOrderBook;

// Bot — base class for all trading agents.
//
// FIX 17: maxPosition field added. Subclasses gate order placement:
//   if (position < maxPosition)  → may buy
//   if (position > -maxPosition) → may sell
//   Without this, bots in trending markets accumulate infinite positions
//   and P&L numbers become meaningless (MomBot1's $531k was caused by this).
//
// FIX 1 (avgCostBasis): recordTrade() now correctly updates avgCostBasis
//   on every buy via weighted average. realizedPnl is now accurate.

class Bot {
public:
    std::string name;
    double cash        = 100000.0;
    int    position    = 0;
    double realizedPnl = 0.0;
    int    maxPosition;   // FIX 17: position limit, set by subclass constructor

    explicit Bot(std::string n, int maxPos = 10)
        : name(std::move(n)), maxPosition(maxPos) {}

    virtual ~Bot() = default;

    virtual void onPriceUpdate(double price, LimitOrderBook& lob, int timestep);
    virtual void onPriceUpdate(double price, FlatOrderBook&  lob, int timestep);

    // recordTrade — called by the matching engine after each fill.
    // Updates position, cash, avgCostBasis, and realizedPnl correctly.
    void recordTrade(double price, int qty, bool buyer) {
        if (buyer) {
            // Weighted average cost basis:
            //   newAvg = (oldAvg * oldQty + price * newQty) / (oldQty + newQty)
            double totalCost  = avgCostBasis * position + price * qty;
            position         += qty;
            avgCostBasis      = (position > 0) ? totalCost / position : 0.0;
            cash             -= price * qty;
        } else {
            // Realised P&L = (sell price - average cost) * qty sold
            realizedPnl += (price - avgCostBasis) * qty;
            cash        += price * qty;
            position    -= qty;
            // Reset cost basis when flat — avoids stale value carrying into
            // the next long position
            if (position == 0) avgCostBasis = 0.0;
        }
    }

    // Total P&L including open position marked to current market price
    double pnl(double currentPrice) const {
        return realizedPnl + (cash - 100000.0) + position * currentPrice;
    }

    double getAvgCostBasis() const { return avgCostBasis; }

private:
    // Average cost basis of the current open position.
    // Updated on every buy via weighted average. Reset to 0 when flat.
    double avgCostBasis = 0.0;
};