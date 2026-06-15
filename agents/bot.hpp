#pragma once
#include <string>
#include <algorithm>

class LimitOrderBook;
class FlatOrderBook;

class Bot {
public:
    std::string name;
    double cash        = 100000.0;
    int    position    = 0;
    double realizedPnl = 0.0;

    Bot(std::string n) : name(n) {}
    virtual ~Bot() = default;

    virtual void onPriceUpdate(double price, LimitOrderBook& lob, int timestep);
    virtual void onPriceUpdate(double price, FlatOrderBook&  lob, int timestep);

    void recordTrade(double price, int qty, bool buyer) {
        if (buyer) {
            // Update average cost basis using weighted average:
            //   newAvg = (oldAvg * oldPosition + price * qty) / newPosition
            // Must compute before updating position.
            double totalCost  = avgCostBasis * position + price * qty;
            position         += qty;
            avgCostBasis      = (position > 0) ? totalCost / position : 0.0;
            cash             -= price * qty;
        } else {
            // Realised P&L = (sell price - average cost) * qty sold
            realizedPnl += (price - avgCostBasis) * qty;
            cash        += price * qty;
            position    -= qty;
            // Reset cost basis when flat — avoids carrying stale value
            // into the next long position.
            if (position == 0) avgCostBasis = 0.0;
        }
    }

    // Mark-to-market P&L:
    //   realizedPnl  = locked-in profit from closed positions
    //   (cash - 100000) = net cash flow from all trades
    //   position * currentPrice = unrealised value of open position
    double pnl(double currentPrice) const {
        return realizedPnl + (cash - 100000.0) + position * currentPrice;
    }

    double getAvgCostBasis() const { return avgCostBasis; }

private:
    // Average cost basis of the current open position.
    // Updated on every buy via weighted average.
    // Reset to 0.0 when position reaches zero.
    double avgCostBasis = 0.0;
};