#include <iostream>
#include "orderbook.hpp"
#include "matching_engine.hpp"

int main() {
    LimitOrderBook lob;

    // Add some test orders
    lob.addOrder({1, "Agent1", 50000, 3}, true);  // Buy
    lob.addOrder({2, "Agent2", 50100, 2}, false); // Sell
    lob.addOrder({3, "Agent3", 49950, 1}, true);  // Buy

    // Match orders and print trades
    matchOrders(lob);

    std::cout << "\nCurrent Order Book:\n";
    std::cout << "Bids:\n";
    for (auto &p : lob.bids) std::cout << p.first << " : " << p.second.size() << " orders\n";
    std::cout << "Asks:\n";
    for (auto &p : lob.asks) std::cout << p.first << " : " << p.second.size() << " orders\n";

    return 0;
}
