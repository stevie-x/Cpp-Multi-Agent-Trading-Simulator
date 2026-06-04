#pragma once
#include <vector>
#include <memory>
#include <atomic>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <chrono>
#include <csignal>

#include "websocket_feed.hpp"
#include "orderbook.hpp"
#include "matching_engine.hpp"
#include "latency_histogram.hpp"
#include "transaction_cost.hpp"
#include "../agents/bot.hpp"

// ── LiveSim: real-time simulation on Binance WebSocket feed ───────────────────
//
// Replaces MarketData::loadCSV() with a live Binance connection.
// Architecture is identical to your existing sim — only the price source changes:
//
//   CSV file  →  Binance WebSocket (ethusdt@trade + ethusdt@depth5)
//
// Each incoming trade tick:
//   1. All bots call onPriceUpdate() with the live price
//   2. matchOrders() runs the matching engine
//   3. Latency histogram records rdtsc timing
//   4. Stats printed every 10 ticks to console
//
// Ctrl+C cleanly shuts down the WebSocket and prints final results.

// Global flag for Ctrl+C shutdown
static std::atomic<bool> g_liveRunning{true};
static void sigHandler(int) { g_liveRunning = false; }

class LiveSim {
public:
    LiveSim(std::vector<std::unique_ptr<Bot>>& bots,
            int maxTicks = 0)           // 0 = run until Ctrl+C
        : bots_(bots), maxTicks_(maxTicks) {}

    void run() {
        std::signal(SIGINT, sigHandler);

        TransactionCostModel costModel;
        LatencyHistogram hist("live: order-created → fill");
        LimitOrderBook lob;
        std::string tradeBuffer = "timestep,buyer,seller,price,quantity\n";
        int tickCount = 0;
        double lastPrice = 0.0;

        auto wallStart = std::chrono::high_resolution_clock::now();

        std::cout << "\n━━━ LIVE SIM: Binance ETH/USDT ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
        std::cout << "  Press Ctrl+C to stop\n\n";

        BinanceWebSocketFeed feed;

        feed.onTick = [&](const MarketTick& tick) {
            if (!g_liveRunning) return;
            if (maxTicks_ > 0 && tickCount >= maxTicks_) {
                g_liveRunning = false;
                return;
            }

            lastPrice = tick.price;
            ++tickCount;

            // Feed live price to all bots
            for (auto& bot : bots_)
                bot->onPriceUpdate(tick.price, lob, tickCount);

            matchOrders(lob, bots_, tradeBuffer, tickCount, &hist, &costModel);

            // Print stats every 10 ticks
            if (tickCount % 10 == 0) {
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::high_resolution_clock::now() - wallStart).count();

                std::cout << std::fixed << std::setprecision(2);
                std::cout << "  [t=" << std::setw(4) << tickCount << "]"
                          << "  ETH $" << std::setw(9) << tick.price
                          << "  imb=" << std::setw(6) << tick.imbalance()
                          << "  elapsed=" << elapsed << "s\n";

                for (auto& bot : bots_) {
                    std::cout << "    " << std::left << std::setw(12) << bot->name
                              << " pnl=$" << std::right << std::setw(8) << bot->pnl(lastPrice)
                              << "  pos=" << std::setw(4) << bot->position << "\n";
                }
                std::cout << "\n";
            }
        };

        feed.start();

        // Block until Ctrl+C or maxTicks reached
        while (g_liveRunning) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        feed.stop();

        // Save trade log
        std::ofstream("data/trade_log_live.csv") << tradeBuffer;

        // Final results
        auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::high_resolution_clock::now() - wallStart).count();

        std::cout << "\n━━━ LIVE SIM RESULTS ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
        std::cout << "  Ticks: " << tickCount << "  |  Runtime: " << totalMs / 1000.0 << "s"
                  << "  |  Last price: $" << lastPrice << "\n\n";

        std::cout << std::fixed << std::setprecision(2);
        for (auto& bot : bots_) {
            std::cout << "  " << std::left << std::setw(12) << bot->name
                      << " pnl=$"     << std::setw(10) << bot->pnl(lastPrice)
                      << " sharpe="   << std::setw(7)  << bot->sharpe()
                      << " winrate="  << std::setw(6)  << bot->winRate()
                      << " costs=$"   << std::setw(8)  << bot->totalCostPaid << "\n";
        }

        hist.print(globalClock());
        std::cout << "\n  Trade log saved to data/trade_log_live.csv\n";
    }

private:
    std::vector<std::unique_ptr<Bot>>& bots_;
    int maxTicks_;
};