#include <iostream>
#include "orderbook.hpp"
#include "matching_engine.hpp"
#include "market_data.hpp"
#include "../agents/random_bot.hpp"   // ← only once, with correct path

int main() {

    MarketData data;
    data.loadCSV("data/eth_1m.csv");

    LimitOrderBook lob;

    RandomBot bot1("BotA");
    RandomBot bot2("BotB");
    RandomBot bot3("BotC");

    int timestep = 0;

    while(data.hasNext()) {

        auto tick = data.next();
        timestep++;

        std::cout << "\n=== Timestep " << timestep << " ===\n";
        std::cout << "Price: " << tick.price << "\n";

        bot1.onPriceUpdate(tick.price, lob, timestep);
        bot2.onPriceUpdate(tick.price, lob, timestep);
        bot3.onPriceUpdate(tick.price, lob, timestep);

        matchOrders(lob);

        if(timestep > 100) break;
    }

    return 0;
}