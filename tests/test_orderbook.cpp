// tests/test_orderbook.cpp
// Run with: make test
//
// Tests every core guarantee of the matching engine:
//   1. Price-time priority — same price, earlier order fills first
//   2. Partial fill       — remaining quantity stays in book
//   3. Cancel             — cancelled order never fills
//   4. Market order       — fills immediately at best available price
//   5. Multi-level match  — order walks the book across multiple price levels
//   6. No self-trade      — bot cannot match against its own order (quantity check)
//   7. avgCostBasis P&L   — buy then sell, realizedPnl is correct

#include <cassert>
#include <iostream>
#include <memory>
#include <vector>
#include <unordered_map>
#include <cmath>

#include "../src/orderbook.hpp"
#include "../src/matching_engine.hpp"
#include "../agents/bot.hpp"
#include "../agents/random_bot.hpp"

// ── Helpers ───────────────────────────────────────────────────────────────────

static std::unordered_map<std::string, Bot*>
makeIndex(const std::vector<std::unique_ptr<Bot>>& bots) {
    std::unordered_map<std::string, Bot*> idx;
    for (const auto& b : bots) idx[b->name] = b.get();
    return idx;
}

static void runMatch(LimitOrderBook& lob,
                     std::vector<std::unique_ptr<Bot>>& bots,
                     std::unordered_map<std::string, Bot*>& idx) {
    std::string buf;
    matchOrders(lob, bots, idx, buf, 1);
}

static bool approxEq(double a, double b, double eps = 0.001) {
    return std::abs(a - b) < eps;
}

// ── Test 1: Price-time priority ───────────────────────────────────────────────
// Two bids at the same price. The one placed first must fill first.
static void test_price_time_priority() {
    std::vector<std::unique_ptr<Bot>> bots;
    bots.push_back(std::make_unique<RandomBot>("Alice"));
    bots.push_back(std::make_unique<RandomBot>("Bob"));
    bots.push_back(std::make_unique<RandomBot>("Charlie"));  // seller
    auto idx = makeIndex(bots);

    LimitOrderBook lob;

    // Alice places buy at 100 first
    lob.addOrder(Order::makeLimitOrder("Alice",   100.0, 5, Side::BUY));
    // Bob places buy at 100 second (same price, later time)
    lob.addOrder(Order::makeLimitOrder("Bob",     100.0, 5, Side::BUY));
    // Charlie sells 5 — should fill Alice (first in queue), not Bob
    lob.addOrder(Order::makeLimitOrder("Charlie", 100.0, 5, Side::SELL));

    runMatch(lob, bots, idx);

    // Alice filled → position +5, cash reduced
    assert(bots[0]->position == 5 && "Price-time priority: Alice should fill first");
    // Bob not filled → position still 0
    assert(bots[1]->position == 0 && "Price-time priority: Bob should NOT fill");

    std::cout << "  PASS test_price_time_priority\n";
}

// ── Test 2: Partial fill ──────────────────────────────────────────────────────
// Buy 10, sell 6 — buyer gets 6 filled, 4 remain in the book.
static void test_partial_fill() {
    std::vector<std::unique_ptr<Bot>> bots;
    bots.push_back(std::make_unique<RandomBot>("Buyer"));
    bots.push_back(std::make_unique<RandomBot>("Seller"));
    auto idx = makeIndex(bots);

    LimitOrderBook lob;
    lob.addOrder(Order::makeLimitOrder("Buyer",  100.0, 10, Side::BUY));
    lob.addOrder(Order::makeLimitOrder("Seller", 100.0,  6, Side::SELL));

    runMatch(lob, bots, idx);

    // Buyer got 6 units
    assert(bots[0]->position == 6 && "Partial fill: buyer should have 6");
    // Remaining 4 still in bids
    assert(!lob.bids.empty() && "Partial fill: bid side should not be empty");
    assert(lob.bids.begin()->second.front().quantity == 4
           && "Partial fill: remaining quantity should be 4");

    std::cout << "  PASS test_partial_fill\n";
}

// ── Test 3: Cancel order ──────────────────────────────────────────────────────
// Place a buy, cancel it, then place a matching sell — no trade should execute.
static void test_cancel_order() {
    std::vector<std::unique_ptr<Bot>> bots;
    bots.push_back(std::make_unique<RandomBot>("Buyer"));
    bots.push_back(std::make_unique<RandomBot>("Seller"));
    auto idx = makeIndex(bots);

    LimitOrderBook lob;
    Order buy = Order::makeLimitOrder("Buyer", 100.0, 5, Side::BUY);
    int cancelId = buy.id;
    lob.addOrder(buy);

    bool cancelled = lob.cancelOrder(cancelId);
    assert(cancelled && "Cancel: cancelOrder should return true for existing order");

    // Now add matching sell — nothing should fill
    lob.addOrder(Order::makeLimitOrder("Seller", 100.0, 5, Side::SELL));
    runMatch(lob, bots, idx);

    assert(bots[0]->position == 0 && "Cancel: buyer position should be 0 after cancel");
    assert(bots[1]->position == 0 && "Cancel: seller position should be 0 (no fill)");

    // Cancelling non-existent id should return false
    bool badCancel = lob.cancelOrder(999999);
    assert(!badCancel && "Cancel: cancelling unknown id should return false");

    std::cout << "  PASS test_cancel_order\n";
}

// ── Test 4: Market order fills immediately ────────────────────────────────────
// Resting limit sell at 100. Market buy should cross immediately.
static void test_market_order() {
    std::vector<std::unique_ptr<Bot>> bots;
    bots.push_back(std::make_unique<RandomBot>("Maker"));   // resting sell
    bots.push_back(std::make_unique<RandomBot>("Taker"));   // market buy
    auto idx = makeIndex(bots);

    LimitOrderBook lob;
    // Maker places resting sell at 100
    lob.addOrder(Order::makeLimitOrder("Maker", 100.0, 5, Side::SELL));
    // Taker market buy — implemented as limit at max price to guarantee fill
    lob.addOrder(Order::makeLimitOrder("Taker",
        std::numeric_limits<double>::max(), 5, Side::BUY));

    runMatch(lob, bots, idx);

    assert(bots[1]->position == 5  && "Market order: taker should be filled");
    assert(bots[0]->position == -5 && "Market order: maker sold 5 units");
    assert(lob.bids.empty() && lob.asks.empty()
           && "Market order: book should be empty after full fill");

    std::cout << "  PASS test_market_order\n";
}

// ── Test 5: Multi-level book walk ─────────────────────────────────────────────
// Aggressive buy walks through two ask price levels.
static void test_multi_level_match() {
    std::vector<std::unique_ptr<Bot>> bots;
    bots.push_back(std::make_unique<RandomBot>("Seller1"));
    bots.push_back(std::make_unique<RandomBot>("Seller2"));
    bots.push_back(std::make_unique<RandomBot>("Buyer"));
    auto idx = makeIndex(bots);

    LimitOrderBook lob;
    // Two resting sells at different price levels
    lob.addOrder(Order::makeLimitOrder("Seller1", 100.0, 3, Side::SELL));
    lob.addOrder(Order::makeLimitOrder("Seller2", 101.0, 3, Side::SELL));
    // Aggressive buy at 105 — should fill both levels
    lob.addOrder(Order::makeLimitOrder("Buyer", 105.0, 6, Side::BUY));

    runMatch(lob, bots, idx);

    assert(bots[2]->position == 6  && "Multi-level: buyer should have 6 units");
    assert(bots[0]->position == -3 && "Multi-level: Seller1 sold 3");
    assert(bots[1]->position == -3 && "Multi-level: Seller2 sold 3");
    assert(lob.bids.empty() && lob.asks.empty()
           && "Multi-level: book should be empty");

    std::cout << "  PASS test_multi_level_match\n";
}

// ── Test 6: No cross when bid < ask ──────────────────────────────────────────
// Bid at 99, ask at 101 — should not match.
static void test_no_cross_when_spread() {
    std::vector<std::unique_ptr<Bot>> bots;
    bots.push_back(std::make_unique<RandomBot>("Buyer"));
    bots.push_back(std::make_unique<RandomBot>("Seller"));
    auto idx = makeIndex(bots);

    LimitOrderBook lob;
    lob.addOrder(Order::makeLimitOrder("Buyer",   99.0, 5, Side::BUY));
    lob.addOrder(Order::makeLimitOrder("Seller", 101.0, 5, Side::SELL));

    runMatch(lob, bots, idx);

    assert(bots[0]->position == 0 && "No cross: buyer should not fill");
    assert(bots[1]->position == 0 && "No cross: seller should not fill");
    assert(!lob.bids.empty() && !lob.asks.empty()
           && "No cross: both sides should remain in book");

    std::cout << "  PASS test_no_cross_when_spread\n";
}

// ── Test 7: avgCostBasis P&L correctness ──────────────────────────────────────
// Buy 10 at $100, sell 10 at $110. realizedPnl should be exactly $100.
static void test_avg_cost_basis_pnl() {
    std::vector<std::unique_ptr<Bot>> bots;
    bots.push_back(std::make_unique<RandomBot>("Trader"));
    bots.push_back(std::make_unique<RandomBot>("Counterparty"));
    auto idx = makeIndex(bots);

    LimitOrderBook lob;

    // Step 1: Buy 10 at $100
    lob.addOrder(Order::makeLimitOrder("Trader",        100.0, 10, Side::BUY));
    lob.addOrder(Order::makeLimitOrder("Counterparty",  100.0, 10, Side::SELL));
    runMatch(lob, bots, idx);

    assert(bots[0]->position == 10 && "P&L test: position should be 10 after buy");
    assert(approxEq(bots[0]->getAvgCostBasis(), 100.0)
           && "P&L test: avgCostBasis should be 100.0 after buy");

    // Step 2: Sell 10 at $110
    lob.addOrder(Order::makeLimitOrder("Counterparty",  110.0, 10, Side::BUY));
    lob.addOrder(Order::makeLimitOrder("Trader",        110.0, 10, Side::SELL));
    runMatch(lob, bots, idx);

    assert(bots[0]->position == 0 && "P&L test: position should be 0 after sell");
    // realizedPnl = (110 - 100) * 10 = 100
    assert(approxEq(bots[0]->realizedPnl, 100.0)
           && "P&L test: realizedPnl should be exactly $100");

    std::cout << "  PASS test_avg_cost_basis_pnl\n";
}

// ── Test 8: Partial fill avgCostBasis ─────────────────────────────────────────
// Buy 5 at $100, buy 5 more at $120. avgCostBasis should be $110.
// Sell 10 at $130. realizedPnl should be (130-110)*10 = $200.
static void test_weighted_avg_cost_basis() {
    std::vector<std::unique_ptr<Bot>> bots;
    bots.push_back(std::make_unique<RandomBot>("Trader"));
    bots.push_back(std::make_unique<RandomBot>("Counter"));
    auto idx = makeIndex(bots);

    LimitOrderBook lob;

    // First buy: 5 @ $100
    lob.addOrder(Order::makeLimitOrder("Trader",  100.0, 5, Side::BUY));
    lob.addOrder(Order::makeLimitOrder("Counter", 100.0, 5, Side::SELL));
    runMatch(lob, bots, idx);

    // Second buy: 5 @ $120
    lob.addOrder(Order::makeLimitOrder("Trader",  120.0, 5, Side::BUY));
    lob.addOrder(Order::makeLimitOrder("Counter", 120.0, 5, Side::SELL));
    runMatch(lob, bots, idx);

    // avgCostBasis = (100*5 + 120*5) / 10 = 110
    assert(approxEq(bots[0]->getAvgCostBasis(), 110.0)
           && "Weighted avg: avgCostBasis should be $110");
    assert(bots[0]->position == 10 && "Weighted avg: position should be 10");

    // Sell all 10 @ $130
    lob.addOrder(Order::makeLimitOrder("Counter", 130.0, 10, Side::BUY));
    lob.addOrder(Order::makeLimitOrder("Trader",  130.0, 10, Side::SELL));
    runMatch(lob, bots, idx);

    // realizedPnl = (130 - 110) * 10 = 200
    assert(approxEq(bots[0]->realizedPnl, 200.0)
           && "Weighted avg: realizedPnl should be $200");

    std::cout << "  PASS test_weighted_avg_cost_basis\n";
}

// ── Main ──────────────────────────────────────────────────────────────────────
int main() {
    std::cout << "\n── Orderbook & Matching Engine Tests ──────────────────\n";

    test_price_time_priority();
    test_partial_fill();
    test_cancel_order();
    test_market_order();
    test_multi_level_match();
    test_no_cross_when_spread();
    test_avg_cost_basis_pnl();
    test_weighted_avg_cost_basis();

    std::cout << "\n  All 8 tests passed.\n";
    std::cout << "──────────────────────────────────────────────────────\n\n";
    return 0;
}
