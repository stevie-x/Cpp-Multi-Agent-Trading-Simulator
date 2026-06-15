#include "flat_matching_engine.hpp"
#include <algorithm>

// matchOrdersFlat — hot path, zero I/O.
//
// FIX 1: findBot() removed. botIndex passed in from caller — O(1) lookup.
// FIX 2: std::cout removed. Trade lines appended to tradeBuffer string only.
//
// Both fixes mirror matching_engine.cpp exactly — keep them in sync.

void matchOrdersFlat(FlatOrderBook& lob,
                     std::vector<std::unique_ptr<Bot>>& bots,
                     std::unordered_map<std::string, Bot*>& botIndex,
                     std::string& tradeBuffer,
                     int timestep) {

    while (!lob.bidsEmpty() && !lob.asksEmpty()) {
        PriceLevel& bidLevel = lob.bestBid();
        PriceLevel& askLevel = lob.bestAsk();

        // Skip cancelled orders (lazy deletion)
        while (!bidLevel.orders.empty() &&
               bidLevel.orders.front().quantity == 0)
            bidLevel.orders.pop_front();
        if (bidLevel.orders.empty()) { lob.removeBestBid(); continue; }

        while (!askLevel.orders.empty() &&
               askLevel.orders.front().quantity == 0)
            askLevel.orders.pop_front();
        if (askLevel.orders.empty()) { lob.removeBestAsk(); continue; }

        if (bidLevel.price >= askLevel.price) {
            Order& buy  = bidLevel.orders.front();
            Order& sell = askLevel.orders.front();

            int    qty   = std::min(buy.quantity, sell.quantity);
            double price = sell.price;

            // No stdout — append to buffer only
            tradeBuffer += std::to_string(timestep)  + ','
                         + buy.agent                 + ','
                         + sell.agent                + ','
                         + std::to_string(price)     + ','
                         + std::to_string(qty)       + '\n';

            // O(1) bot lookup — no linear scan
            auto buyIt  = botIndex.find(buy.agent);
            auto sellIt = botIndex.find(sell.agent);
            if (buyIt  != botIndex.end()) buyIt->second->recordTrade(price, qty, true);
            if (sellIt != botIndex.end()) sellIt->second->recordTrade(price, qty, false);

            buy.quantity  -= qty;
            sell.quantity -= qty;

            if (buy.quantity == 0) {
                lob.orderIndex.erase(buy.id);
                bidLevel.orders.pop_front();
            }
            if (sell.quantity == 0) {
                lob.orderIndex.erase(sell.id);
                askLevel.orders.pop_front();
            }

            if (bidLevel.orders.empty())  lob.removeBestBid();
            if (askLevel.orders.empty())  lob.removeBestAsk();
        } else {
            break;
        }
    }
}