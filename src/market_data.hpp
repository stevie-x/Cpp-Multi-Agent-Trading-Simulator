#pragma once
#include <vector>
#include <string>

struct MarketTick {
    long long timestamp;
    double price;
};

class MarketData {
public:
    std::vector<MarketTick> ticks;
    size_t index = 0;   // Fix #5: size_t matches ticks.size() type — no signed/unsigned mismatch

    bool loadCSV(const std::string& filename);
    MarketTick next();
    bool hasNext();
};