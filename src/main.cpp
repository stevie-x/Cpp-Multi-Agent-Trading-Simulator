#include <iostream>
#include <fstream>
#include <vector>
#include <memory>
#include <iomanip>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <chrono>
#include <thread>

#include "orderbook.hpp"
#include "matching_engine.hpp"
#include "market_data.hpp"
#include "threaded_sim.hpp"
#include "rdtsc.hpp"
#include "latency_histogram.hpp"
#include "transaction_cost.hpp"
#include "fix_parser.hpp"
#include "order_pool.hpp"

#include "../agents/random_bot.hpp"
#include "../agents/momentum_bot.hpp"
#include "../agents/rsi_bot.hpp"
#include "../agents/bollinger_bot.hpp"
#include "../agents/imbalance_bot.hpp"
#include "live_sim.hpp"

static std::vector<std::unique_ptr<Bot>> makeBots() {
    std::vector<std::unique_ptr<Bot>> bots;
    bots.push_back(std::make_unique<RandomBot>("Random_A"));
    bots.push_back(std::make_unique<RandomBot>("Random_B"));
    bots.push_back(std::make_unique<MomentumBot>("Momentum", 10));
    bots.push_back(std::make_unique<RSIBot>("RSI", 14, 30.0, 70.0));
    bots.push_back(std::make_unique<BollingerBot>("Bollinger", 20, 2.0));
    bots.push_back(std::make_unique<ImbalanceBot>("Imbalance", 0.25));
    return bots;
}

static void printComparisonTable(const std::vector<std::unique_ptr<Bot>>& bots,
                                 double lastPrice) {
    std::cout << "\n╔═══════════════════════════════════════════════════════════════════════════════════╗\n";
    std::cout <<   "║  Strategy Comparison                                                              ║\n";
    std::cout <<   "╠══════════════╦═══════════╦══════════╦══════════╦══════════╦═══════════╦══════════╣\n";
    std::cout <<   "║  Bot         ║  PnL ($)  ║  Sharpe  ║  WinRate ║ MaxDrawd ║  Costs($) ║  Pos     ║\n";
    std::cout <<   "╠══════════════╬═══════════╬══════════╬══════════╬══════════╬═══════════╬══════════╣\n";
    std::cout << std::fixed << std::setprecision(2);
    for (const auto& b : bots) {
        std::cout << "║  " << std::left  << std::setw(12) << b->name
                  << "║ " << std::right << std::setw(9)  << b->pnl(lastPrice)
                  << " ║ "              << std::setw(8)  << b->sharpe()
                  << " ║ "             << std::setw(8)  << b->winRate()
                  << " ║ "              << std::setw(8)  << b->maxDrawdown()
                  << " ║ "              << std::setw(9)  << b->totalCostPaid
                  << " ║ "              << std::setw(8)  << b->position
                  << " ║\n";
    }
    std::cout << "╚══════════════╩═══════════╩══════════╩══════════╩══════════╩═══════════╩══════════╝\n";
    std::cout << "  (Last price: $" << lastPrice << ")\n";
}

static void runFixDemo() {
    std::cout << "\n━━━ FIX 4.2 PROTOCOL DEMO ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
    FixBuilder builder;

    std::string nos = builder.newOrderSingle("ORD-001", "BollingerBot", "ETHUSD", Side::BUY, 2400.50, 1);
    std::cout << "Outgoing NewOrderSingle:\n  " << nos << "\n";

    FixMessage parsed;
    if (parsed.parse(nos)) {
        std::cout << "Parsed:\n";
        std::cout << "  MsgType : " << parsed.msgType() << " (D=NewOrderSingle)\n";
        std::cout << "  Sender  : " << parsed.sender()  << "\n";
        std::cout << "  Symbol  : " << parsed.symbol()  << "\n";
        std::cout << "  Side    : " << (parsed.side() == Side::BUY ? "BUY" : "SELL") << "\n";
        std::cout << "  Price   : " << parsed.price()   << "\n";
        std::cout << "  Qty     : " << parsed.qty()     << "\n";
        Order ord = fixToOrder(parsed);
        std::cout << "  → Order id=" << ord.id << " price=" << ord.price << " qty=" << ord.quantity << "\n";
    }

    std::string er = builder.executionReport("ORD-001", "Exchange", 2400.50, 1);
    std::cout << "\nIncoming ExecutionReport:\n  " << er << "\n";
    FixMessage erMsg;
    if (erMsg.parse(er)) {
        std::cout << "Parsed:\n";
        std::cout << "  ExecType : " << erMsg.get(150) << " (2=Fill)\n";
        std::cout << "  OrdStatus: " << erMsg.get(39)  << " (2=Filled)\n";
        std::cout << "  AvgPx    : " << erMsg.get(6)   << "\n";
        std::cout << "  CumQty   : " << erMsg.get(14)  << "\n";
    }
}

static void runPoolDemo() {
    std::cout << "\n━━━ OBJECT POOL DEMO ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
    OrderPool<1024> pool;
    std::cout << "Pool capacity : " << pool.capacity() << "\n";

    std::vector<Order*> batch;
    for (int i = 0; i < 10; ++i) {
        Order* o = pool.acquire();
        *o = Order::makeLimitOrder("PoolBot", 2400.0 + i, 1, Side::BUY);
        batch.push_back(o);
    }
    std::cout << "After 10 acquires: used=" << pool.used() << "\n";
    for (auto* o : batch) pool.release(o);
    std::cout << "After 10 releases: free=" << pool.free() << " (back to full)\n";
    std::cout << "alignof(Order)  = " << alignof(Order) << " bytes\n";
}

int main() {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    std::cout << "Calibrating TSC clock... ";
    globalClock().calibrate();
    std::cout << std::fixed << std::setprecision(3) << globalClock().ns_per_cycle << " ns/cycle\n";

    constexpr int LIMIT = 500;

    std::cout << "\n╔══════════════════════════════════════════════════════════════════════════════╗\n";
    std::cout <<   "║  C++ Multi-Agent Trading Simulator v2                                      ║\n";
    std::cout <<   "║  6 Agents: Random×2, Momentum(regime), RSI(regime), Bollinger, Imbalance  ║\n";
    std::cout <<   "║  Features: rdtsc latency, object pool, queue-pos fills, tx costs, FIX     ║\n";
    std::cout <<   "╚══════════════════════════════════════════════════════════════════════════════╝\n";

    TransactionCostModel costModel;
    LatencyHistogram matchHist("order-created → fill");

    std::cout << "\n━━━ ETH/USDT ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
    {
        MarketData data;
        data.loadCSV("data/eth_1m.csv");

        auto bots = makeBots();
        LimitOrderBook lob;
        std::string tradeBuffer = "timestep,buyer,seller,price,quantity\n";
        std::string priceBuffer = "timestep,price\n";
        int timestep = 0;
        double lastPrice = 0.0;

        auto wallStart = std::chrono::high_resolution_clock::now();

        while (data.hasNext() && timestep < LIMIT) {
            auto tick = data.next();
            lastPrice = tick.price;
            ++timestep;
            priceBuffer += std::to_string(timestep) + ',' + std::to_string(tick.price) + '\n';
            for (auto& bot : bots)
                bot->onPriceUpdate(tick.price, lob, timestep);
            matchOrders(lob, bots, tradeBuffer, timestep, &matchHist, &costModel);
        }

        auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                      std::chrono::high_resolution_clock::now() - wallStart).count();

        std::ofstream("data/trade_log_ETH.csv") << tradeBuffer;
        std::ofstream("data/price_log_ETH.csv") << priceBuffer;

        std::cout << "  Ticks: " << timestep << "  |  Time: " << us << " µs"
                  << "  |  Throughput: " << std::fixed << std::setprecision(0)
                  << (timestep / (us / 1e6)) << " ticks/sec\n";

        printComparisonTable(bots, lastPrice);
        matchHist.print(globalClock());

        std::cout << "\n── Walk-Forward Split (train=70% | test=30%) ─────────────────────────────\n";
        std::cout << "  Train: ticks 1–350  |  Test: ticks 351–500\n";
        std::cout << "  Tip: instantiate fresh bots on each segment to detect overfitting\n";
    }

    runFixDemo();
    runPoolDemo();

    std::cout << "\n━━━ REGIME DETECTION ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
    std::cout << "  Momentum  → TRENDING only    (positive lag-1 autocorrelation)\n";
    std::cout << "  RSI       → MEAN_REVERT only (negative lag-1 autocorrelation)\n";
    std::cout << "  Bollinger → volatility-normalised; maker/taker spread decision\n";
    std::cout << "  Imbalance → 5-level book signal; backs off >60% toxic fills\n";
    std::cout << "  Tx costs  → maker 10bps, taker 10bps, impact 5bps*sqrt(qty/vol)\n";

    std::cout << "\n━━━ MULTITHREADED PIPELINE ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
    std::cout << "  (skipped — see ThreadedSim for lock-free SPSC queue implementation)\n";

    std::cout << "\n╔══════════════════════════════════════════════════════════════════════════════╗\n";
    std::cout <<   "║  rdtsc calibrated  │  object pool (zero malloc)  │  FIX 4.2 parser        ║\n";
    std::cout <<   "║  alignas(64) Order │  queue-pos fill sim         │  transaction costs      ║\n";
    std::cout <<   "║  O3 march=native   │  regime-gated strategies    │  adverse selection det. ║\n";
    std::cout <<   "╚══════════════════════════════════════════════════════════════════════════════╝\n";

    // ── Live Binance WebSocket feed ───────────────────────────────────────────
    std::cout << "\n━━━ Starting live sim in 3 seconds — Ctrl+C to skip or stop ━━━\n";
    std::this_thread::sleep_for(std::chrono::seconds(3));
    auto liveBots = makeBots();
    LiveSim live(liveBots, 0);
    live.run();

    return 0;
}