#include "matching_engine.hpp"

void matchOrders(LimitOrderBook &lob) {
    while(!lob.bids.empty() && !lob.asks.empty()) {
        auto bestBidIt = lob.bids.begin();
        auto bestAskIt = lob.asks.begin();

        if(bestBidIt->first >= bestAskIt->first) {
            Order &buyOrder = bestBidIt->second.front();
            Order &sellOrder = bestAskIt->second.front();

            int executedQty = std::min(buyOrder.quantity, sellOrder.quantity);
            double tradePrice = sellOrder.price;

            std::cout << "Trade executed: " << executedQty
                      << " BTC at " << tradePrice
                      << " between " << buyOrder.agent
                      << " and " << sellOrder.agent << "\n";

            buyOrder.quantity -= executedQty;
            sellOrder.quantity -= executedQty;

            if(buyOrder.quantity == 0) bestBidIt->second.pop();
            if(sellOrder.quantity == 0) bestAskIt->second.pop();

            if(bestBidIt->second.empty()) lob.bids.erase(bestBidIt);
            if(bestAskIt->second.empty()) lob.asks.erase(bestAskIt);
        } else {
            break;
        }
    }
}
