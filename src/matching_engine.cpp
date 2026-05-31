#include "matching_engine.hpp"
#include <algorithm>
#include <iostream>

static Bot* findBot(std::vector<std::unique_ptr<Bot>>& bots, const std::string& name) {
    for (auto& b : bots)
        if (b->name == name) return b.get();
    return nullptr;
}

void matchOrders(LimitOrderBook& lob,
                 std::vector<std::unique_ptr<Bot>>& bots,
                 std::string& tradeBuffer,
                 int timestep) {

    while (!lob.bids.empty() && !lob.asks.empty()) {
        auto bidIt = lob.bids.begin();
        auto askIt = lob.asks.begin();

        while (!bidIt->second.empty() &&
               bidIt->second.front().quantity == 0)
            bidIt->second.pop_front();
        if (bidIt->second.empty()) { lob.bids.erase(bidIt); continue; }

        while (!askIt->second.empty() &&
               askIt->second.front().quantity == 0)
            askIt->second.pop_front();
        if (askIt->second.empty()) { lob.asks.erase(askIt); continue; }

        if (bidIt->first >= askIt->first) {
            Order& buy  = bidIt->second.front();
            Order& sell = askIt->second.front();

            int    qty   = std::min(buy.quantity, sell.quantity);
            double price = sell.price;

            std::cout << "Trade executed: " << qty
                      << " ETH at " << price
                      << " between " << buy.agent
                      << " and "    << sell.agent << '\n';

            tradeBuffer += std::to_string(timestep) + ','
                         + buy.agent  + ','
                         + sell.agent + ','
                         + std::to_string(price) + ','
                         + std::to_string(qty)   + '\n';

            if (Bot* buyer  = findBot(bots, buy.agent))
                buyer->recordTrade(price, qty, true);
            if (Bot* seller = findBot(bots, sell.agent))
                seller->recordTrade(price, qty, false);

            buy.quantity  -= qty;
            sell.quantity -= qty;

            if (buy.quantity  == 0) {
                lob.orderIndex.erase(buy.id);
                bidIt->second.pop_front();
            }
            if (sell.quantity == 0) {
                lob.orderIndex.erase(sell.id);
                askIt->second.pop_front();
            }

            if (bidIt->second.empty()) lob.bids.erase(bidIt);
            if (askIt->second.empty()) lob.asks.erase(askIt);
        } else {
            break;
        }
    }
}