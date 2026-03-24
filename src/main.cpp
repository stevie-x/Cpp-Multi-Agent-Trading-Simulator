#include <iostream>
#include <fstream>
#include <vector>
#include <memory>
#include "orderbook.hpp"
#include "matching_engine.hpp"
#include "market_data.hpp"
#include "../agents/random_bot.hpp"

int main() {
    // Fix #2: decouple cout from stdio for faster console output
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    MarketData data;
    data.loadCSV("data/eth_1m.csv");

    LimitOrderBook lob;

    // Fix #6: vector of bots — easy to add more bot types later
    std::vector<std::unique_ptr<Bot>> bots;
    bots.push_back(std::make_unique<RandomBot>("BotA"));
    bots.push_back(std::make_unique<RandomBot>("BotB"));
    bots.push_back(std::make_unique<RandomBot>("BotC"));

    // Fix #3: use string buffers, write to file once at the end
    std::string tradeBuffer = "timestep,buyer,seller,price,quantity\n";
    std::string priceBuffer = "timestep,price\n";

    int timestep = 0;

    while(data.hasNext()) {
        auto tick = data.next();
        timestep++;

        std::cout << "\n=== Timestep " << timestep << " ===\n"
                  << "Price: " << tick.price << '\n';

        // Fix #3: append to buffer instead of writing to file each tick
        priceBuffer += std::to_string(timestep) + ',' + std::to_string(tick.price) + '\n';

        for(auto& bot : bots)
            bot->onPriceUpdate(tick.price, lob, timestep);

        matchOrders(lob, tradeBuffer, timestep);

        if(timestep > 500) break;
    }

    // Write buffers to disk in one shot
    std::ofstream("data/trade_log.csv")  << tradeBuffer;
    std::ofstream("data/price_log.csv")  << priceBuffer;

    std::cout << "\nLogs saved to data/trade_log.csv and data/price_log.csv\n";
    return 0;
}