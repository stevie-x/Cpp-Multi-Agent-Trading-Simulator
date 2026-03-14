#pragma once
#include "bot.hpp"
#include <cstdlib>

class RandomBot : public Bot {
public:
    RandomBot(std::string n) : Bot(n) {}

    void onPriceUpdate(double price, LimitOrderBook& lob, int timestep) override {
        int r = rand() % 10;

        if(r < 3) {
            // buy slightly below market
            lob.addOrder({timestep, name, price - 5, 1}, true);
        }
        else if(r > 7) {
            // sell slightly above market
            lob.addOrder({timestep + 10000, name, price + 5, 1}, false);
        }
    }
};