#include <iostream>
#include <fstream>
#include <vector>
#include <memory>
#include <iomanip>
#include <chrono>
#include <algorithm>
#include <numeric>
#include <unordered_map>

#include "orderbook.hpp"
#include "matching_engine.hpp"
#include "market_data.hpp"
#include "threaded_sim.hpp"
#include "async_logger.hpp"

#include "../agents/random_bot.hpp"
#include "../agents/momentum_bot.hpp"
#include "../agents/rsi_bot.hpp"
#include "spread_tracker.hpp"

using Clock = std::chrono::high_resolution_clock;
using ns    = std::chrono::nanoseconds;

static std::vector<std::unique_ptr<Bot>> makeBots() {
    std::vector<std::unique_ptr<Bot>> bots;
    bots.push_back(std::make_unique<RandomBot>("BotA"));
    bots.push_back(std::make_unique<RandomBot>("BotB"));
    bots.push_back(std::make_unique<RandomBot>("BotC"));
    bots.push_back(std::make_unique<MomentumBot>("MomBot1",  6));  // even window
    bots.push_back(std::make_unique<MomentumBot>("MomBot2", 10));
    bots.push_back(std::make_unique<RSIBot>("RSIBot1", 14, 30.0, 70.0));
    return bots;
}

// Build O(1) bot lookup index from the bot vector.
// Called once per simulation run — never inside the hot path.
static std::unordered_map<std::string, Bot*>
buildBotIndex(const std::vector<std::unique_ptr<Bot>>& bots) {
    std::unordered_map<std::string, Bot*> index;
    index.reserve(bots.size());
    for (const auto& b : bots)
        index[b->name] = b.get();
    return index;
}

static void printPnL(const std::string& label,
                     const std::vector<std::unique_ptr<Bot>>& bots,
                     double lastPrice) {
    std::cout << "\n╔══════════════════════════════════════════════════════╗\n";
    std::cout <<   "║  P&L — " << std::left << std::setw(45) << label << "║\n";
    std::cout <<   "╠══════════════════════════════════════════════════════╣\n";
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "║ Last price: $" << std::setw(10) << lastPrice
              << "                              ║\n";
    std::cout <<   "╠══════════════════════════════════════════════════════╣\n";
    std::cout <<   "║  Bot          Cash($)      Pos    RealizedPnL($)    ║\n";
    std::cout <<   "╠══════════════════════════════════════════════════════╣\n";
    for (const auto& bot : bots) {
        std::cout << "║  " << std::left  << std::setw(12) << bot->name
                  << std::right << std::setw(11) << bot->cash
                  << std::setw(6)  << bot->position
                  << std::setw(16) << bot->realizedPnl
                  << "    ║\n";
    }
    std::cout << "╚══════════════════════════════════════════════════════╝\n";
}

static void printHistogram(const std::vector<long long>& samples_ns,
                           const std::string& label) {
    if (samples_ns.empty()) return;

    auto sorted = samples_ns;
    std::sort(sorted.begin(), sorted.end());

    auto pct = [&](double p) -> long long {
        size_t idx = (size_t)(p / 100.0 * sorted.size());
        if (idx >= sorted.size()) idx = sorted.size() - 1;
        return sorted[idx];
    };

    double avg = std::accumulate(sorted.begin(), sorted.end(), 0LL) /
                 (double)sorted.size();

    std::cout << "\n── Latency histogram: " << label << " ──────────────────────\n";
    std::cout << "  samples : " << sorted.size() << " ticks\n";
    std::cout << "  min     : " << sorted.front()  << " ns\n";
    std::cout << "  avg     : " << (long long)avg  << " ns\n";
    std::cout << "  p50     : " << pct(50)         << " ns\n";
    std::cout << "  p99     : " << pct(99)         << " ns\n";
    std::cout << "  p99.9   : " << pct(99.9)       << " ns\n";
    std::cout << "  max     : " << sorted.back()   << " ns\n";
    std::cout << "────────────────────────────────────────────────────────\n";
}

static long long runSingleThreaded(const std::string& name,
                                   const std::string& csvPath,
                                   std::vector<std::unique_ptr<Bot>>& bots,
                                   double& lastPrice,
                                   int limit = 500) {
    MarketData data;
    data.loadCSV(csvPath);

    LimitOrderBook lob;

    // Build botIndex once — passed to matchOrders() on every tick
    auto botIndex = buildBotIndex(bots);

    // Use AsyncLogger — no stdout in the hot path
    AsyncLogger tradeLogger("data/trade_log_" + name + ".csv");
    tradeLogger.writeHeader("timestep,buyer,seller,price,quantity\n");

    std::string priceBuffer = "timestep,price\n";
    int timestep = 0;

    std::vector<long long> tickLatencies;
    tickLatencies.reserve(limit);

    // FIX 20: track bid-ask spread across the run
    SpreadTracker spreadTracker;

    auto wallStart = Clock::now();

    while (data.hasNext() && timestep < limit) {
        auto tickStart = Clock::now();

        auto tick = data.next();
        lastPrice = tick.price;
        timestep++;

        priceBuffer += std::to_string(timestep) + ',' +
                       std::to_string(tick.price) + '\n';

        for (auto& bot : bots)
            bot->onPriceUpdate(tick.price, lob, timestep);

        // matchOrders now takes botIndex — O(1) bot lookup, no stdout
        std::string tradeLines;
        matchOrders(lob, bots, botIndex, tradeLines, timestep);
        if (!tradeLines.empty())
            tradeLogger.log(tradeLines);

        // Record spread after each match — captures post-trade book state
        spreadTracker.record(lob);

        tickLatencies.push_back(
            std::chrono::duration_cast<ns>(Clock::now() - tickStart).count());
    }

    tradeLogger.stop();

    auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                  Clock::now() - wallStart).count();

    std::ofstream("data/price_log_" + name + ".csv") << priceBuffer;

    printPnL(name + " (single-threaded)", bots, lastPrice);
    std::cout << "  Ticks: " << timestep
              << "  |  Time: " << us << " µs"
              << "  |  Throughput: " << std::fixed << std::setprecision(0)
              << (timestep / (us / 1e6)) << " ticks/sec\n";

    printHistogram(tickLatencies, name + " single-threaded");
    spreadTracker.print(name);

    return us;
}

int main() {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    constexpr int LIMIT = 500;

    std::cout << "\n╔══════════════════════════════════════════════════════╗\n";
    std::cout <<   "║     C++ Multi-Agent Trading Simulator                ║\n";
    std::cout <<   "║     Single-threaded  vs  4-Thread + Pool Pipeline    ║\n";
    std::cout <<   "╚══════════════════════════════════════════════════════╝\n";

    std::cout << "\n━━━ SINGLE-THREADED ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
    double ethPrice = 0.0, btcPrice = 0.0;
    long long stEth, stBtc;
    { auto bots = makeBots(); stEth = runSingleThreaded("ETH", "data/eth_1m.csv", bots, ethPrice, LIMIT); }
    { auto bots = makeBots(); stBtc = runSingleThreaded("BTC", "data/btc_1m.csv", bots, btcPrice, LIMIT); }

    std::cout << "\n━━━ MULTITHREADED (4-thread pipeline + bot thread pool) ━\n";
    long long mtEth, mtBtc;

    {
        auto bots = makeBots();
        ThreadedSim sim("ETH", "data/eth_1m.csv", bots, LIMIT);
        mtEth = sim.run();
        std::cout << "\n  ETH threaded:"
                  << "  ticks="        << sim.ticksProcessed()
                  << "  orders="       << sim.ordersPlaced()
                  << "  trades="       << sim.tradesExecuted()
                  << "  pool_threads=" << sim.poolThreads()
                  << "  time="         << mtEth << " µs\n";
    }
    {
        auto bots = makeBots();
        ThreadedSim sim("BTC", "data/btc_1m.csv", bots, LIMIT);
        mtBtc = sim.run();
        std::cout << "\n  BTC threaded:"
                  << "  ticks="        << sim.ticksProcessed()
                  << "  orders="       << sim.ordersPlaced()
                  << "  trades="       << sim.tradesExecuted()
                  << "  pool_threads=" << sim.poolThreads()
                  << "  time="         << mtBtc << " µs\n";
    }

    std::cout << "\n╔══════════════════════════════════════════════════════╗\n";
    std::cout <<   "║        SINGLE-THREADED vs MULTITHREADED              ║\n";
    std::cout <<   "╠══════════════════════════════════════════════════════╣\n";
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "║  ETH  single: " << std::setw(8) << stEth
              << " µs   multi: "    << std::setw(8) << mtEth << " µs      ║\n";
    std::cout << "║  BTC  single: " << std::setw(8) << stBtc
              << " µs   multi: "    << std::setw(8) << mtBtc << " µs      ║\n";
    std::cout <<   "╠══════════════════════════════════════════════════════╣\n";
    std::cout <<   "║  Pipeline : Feed→Bots→Matcher  (SPSC, zero mutex)   ║\n";
    std::cout <<   "║  Bot pool : each bot runs concurrently (ThreadPool)  ║\n";
    std::cout <<   "║  Logger   : async Thread 4, never blocks matcher     ║\n";
    std::cout <<   "║  Padding  : alignas(64) stats, no false sharing      ║\n";
    std::cout <<   "╚══════════════════════════════════════════════════════╝\n";

    return 0;
}