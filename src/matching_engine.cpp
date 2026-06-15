#include "matching_engine.hpp"
#include "async_logger.hpp"
#include <algorithm>

// matchOrders — hot path, zero I/O.
//
// FIX 1: findBot() removed. botIndex (unordered_map) passed in from caller,
//         built once at startup — O(1) lookup vs O(N) linear scan.
//
// FIX 2: std::cout removed from hot path. Trade lines are appended to
//         tradeBuffer (a string) and the caller writes to AsyncLogger.
//         Single-threaded benchmarks now measure matching speed, not I/O.

void matchOrders(LimitOrderBook& lob,
                 std::vector<std::unique_ptr<Bot>>& bots,
                 std::unordered_map<std::string, Bot*>& botIndex,
                 std::string& tradeBuffer,
                 int timestep) {

    while (!lob.bids.empty() && !lob.asks.empty()) {
        auto bidIt = lob.bids.begin();
        auto askIt = lob.asks.begin();

        // Skip cancelled orders (lazy deletion — quantity set to 0 by cancelOrder())
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
            double price = sell.price;   // price-time priority: aggressor pays passive price

            // Append to trade buffer — caller writes to AsyncLogger (no syscall here)
            tradeBuffer += std::to_string(timestep) + ','
                         + buy.agent  + ','
                         + sell.agent + ','
                         + std::to_string(price) + ','
                         + std::to_string(qty)   + '\n';

            // O(1) bot lookup via pre-built index — not O(N) linear scan
            auto buyIt  = botIndex.find(buy.agent);
            auto sellIt = botIndex.find(sell.agent);
            if (buyIt  != botIndex.end()) buyIt->second->recordTrade(price, qty, true);
            if (sellIt != botIndex.end()) sellIt->second->recordTrade(price, qty, false);

            buy.quantity  -= qty;
            sell.quantity -= qty;

            if (buy.quantity == 0) {
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
            break;  // no crossable orders — done
        }
    }
}