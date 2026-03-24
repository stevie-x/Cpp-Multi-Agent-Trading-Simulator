#pragma once
#include "bot.hpp"
#include <cstdlib>
#include "../src/orderbook.hpp"

class RandomBot : public Bot {
public:

    RandomBot(std::string n) : Bot(n) {}

    void onPriceUpdate(double price, LimitOrderBook& lob, int timestep) override {

        int r = rand() % 10;

        if(r < 3) {
            // buy
            lob.addOrder({timestep, name, price - 5, 1}, true);
        }
        else if(r > 7) {
            // sell
            lob.addOrder({timestep + 10000, name, price + 5, 1}, false);
        }
    }
};