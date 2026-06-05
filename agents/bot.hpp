#pragma once
#include <string>
#include <vector>
#include <cmath>
#include <iostream>

class LimitOrderBook;
class FlatOrderBook;

class Bot {
public:
    std::string name;
    double cash          = 100000.0;
    int    position      = 0;
    double realizedPnl   = 0.0;
    double totalCostPaid = 0.0;

    // Risk limits
    int    maxPosition    = 20;
    double maxDrawdownLmt = 5000.0;
    bool   halted         = false;

    struct TradeRecord { double pnl; double price; int qty; bool wasBuyer; };
    std::vector<TradeRecord> tradeHistory;

    Bot(std::string n) : name(std::move(n)) {}
    virtual ~Bot() = default;

    virtual void onPriceUpdate(double price, LimitOrderBook& lob, int timestep) = 0;
    virtual void onPriceUpdate(double price, FlatOrderBook&  lob, int timestep) = 0;

    bool riskCheck(double currentPrice) {
        if (halted) return false;
        if (std::abs(position) >= maxPosition) return false;
        double currentPnl = pnl(currentPrice);
        if (currentPnl < peakPnl_ - maxDrawdownLmt) {
            halted = true;
            std::cout << "  [RISK] " << name
                      << " halted — drawdown limit hit ($"
                      << std::fixed << (peakPnl_ - currentPnl) << ")\n";
            return false;
        }
        if (currentPnl > peakPnl_) peakPnl_ = currentPnl;
        return true;
    }

    void recordTrade(double price, int qty, bool buyer) {
        double tradePnl = 0.0;
        if (buyer) {
            cash     -= price * qty;
            position += qty;
            double prev = avgCostBasis_ * (position - qty);
            if (position > 0) avgCostBasis_ = (prev + price * qty) / position;
        } else {
            tradePnl    = (price - avgCostBasis_) * qty;
            realizedPnl += tradePnl;
            cash        += price * qty;
            position    -= qty;
        }
        tradeHistory.push_back({tradePnl, price, qty, buyer});
    }

    double pnl(double currentPrice) const {
        return realizedPnl + (cash - 100000.0) + position * currentPrice;
    }

    double sharpe() const {
        if (tradeHistory.size() < 2) return 0.0;
        double sum = 0.0;
        for (auto& t : tradeHistory) sum += t.pnl;
        double mean = sum / tradeHistory.size();
        double var  = 0.0;
        for (auto& t : tradeHistory) var += (t.pnl - mean) * (t.pnl - mean);
        double std = std::sqrt(var / tradeHistory.size());
        return std == 0.0 ? 0.0 : (mean / std) * std::sqrt(252.0);
    }

    double maxDrawdown() const {
        double peak = 0.0, maxDD = 0.0, running = 0.0;
        for (auto& t : tradeHistory) {
            running += t.pnl;
            if (running > peak) peak = running;
            double dd = peak - running;
            if (dd > maxDD) maxDD = dd;
        }
        return maxDD;
    }

    double winRate() const {
        int wins = 0, total = 0;
        for (auto& t : tradeHistory)
            if (!t.wasBuyer) { total++; if (t.pnl > 0) wins++; }
        return total == 0 ? 0.0 : static_cast<double>(wins) / total;
    }

private:
    double avgCostBasis_ = 0.0;
    double peakPnl_      = 0.0;
};