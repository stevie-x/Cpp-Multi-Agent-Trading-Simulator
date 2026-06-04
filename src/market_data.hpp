// It simulates a live market feed using historical price data.

#pragma once  // Ensures this file is included only once, prevents duplicate definitions
#include <vector>
#include <string>

// Represents one data point (basically, one row of market data)
// Later, use high-low for Volatility, open-close for Momentum, volume for liquidity
struct MarketTick {
    double price     = 0.0;
    double qty       = 0.0;
    double bidVol    = 0.0;
    double askVol    = 0.0;
    long long timestamp = 0;

    double imbalance() const {
        double total = bidVol + askVol;
        return total == 0.0 ? 0.0 : (bidVol - askVol) / total;
    }
};

// This class acts like a data stream simulator:
class MarketData {
public:
    std::vector<MarketTick> ticks;  // stores all market data
    size_t index = 0;   // Fix #5: size_t matches ticks.size() type — no signed/unsigned mismatch

    bool loadCSV(const std::string& filename);  // Loads market data from a CSV file into ticks
    // & is used, means: refernced, don’t copy the string, just refer to original
    // const is used, means: function cannot modify filename
    // bool as we know returns true or false, true here would mean 
    // File opened correctly, Data read correctly, Parsed without errors, Stored into ticks ✅
    // This is only a declaration, not implementation, we would still need to define it in .cpp:

    MarketTick next();  // Give me the next market data point
    bool hasNext();   // Is there more data left?
};