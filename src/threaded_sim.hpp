#pragma once
#include <thread>
#include <atomic>
#include <vector>
#include <memory>
#include <mutex>
#include <chrono>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <unordered_map>

#include "spsc_queue.hpp"
#include "async_logger.hpp"
#include "market_data.hpp"
#include "orderbook.hpp"
#include "order.hpp"
#include "../agents/bot.hpp"
#include "thread_pool.hpp"

// 4-thread pipeline:
//   Thread 1 (feed)    →[SPSC TickQueue]→
//   Thread 2 (bots)    →[SPSC OrderQueue]→
//   Thread 3 (matcher) →[AsyncLogger]→ CSV
//   Thread 4 (logger)  → disk
//
// FIX: ThreadPool now used in botThread() to run each bot's onPriceUpdate()
//      concurrently. Each bot gets its own staging LOB to avoid data races
//      — bots cannot share a single LOB and write to it from multiple threads.
//      After all bot futures resolve, orders from all staging LOBs are merged
//      and flushed to the SPSC orderQueue_.
//
// FIX: matchAndLog() uses botIndex (unordered_map) for O(1) bot lookup —
//      no O(N) linear scan through bots_ vector on every fill.
//
// Zero mutex in hot path (Threads 1-3). False sharing eliminated via
// alignas(64) per-thread stats.

class ThreadedSim {
public:
    struct alignas(64) ThreadStats {
        std::atomic<uint64_t> count{0};
    };

    ThreadedSim(const std::string& instrument,
                const std::string& csvPath,
                std::vector<std::unique_ptr<Bot>>& bots,
                int limit = 500)
        : instrument_(instrument)
        , csvPath_(csvPath)
        , bots_(bots)
        , limit_(limit)
        , done_(false)
        , botDone_(false)
    {
        // Build O(1) bot lookup index once at construction.
        // Passed into matchAndLog() on every call — no linear scan.
        for (auto& b : bots_)
            botIndex_[b->name] = b.get();
    }

    long long run() {
        auto wallStart = std::chrono::high_resolution_clock::now();

        AsyncLogger logger("data/trade_log_" + instrument_ + "_threaded.csv");
        logger.writeHeader("timestep,buyer,seller,price,quantity\n");

        std::thread t1([this]          { feedThread(); });
        std::this_thread::sleep_for(std::chrono::microseconds(100));
        std::thread t2([this]          { botThread(); });
        std::thread t3([this, &logger] { matchThread(logger); });

        t1.join();
        t2.join();
        t3.join();
        logger.stop();

        auto wallEnd = std::chrono::high_resolution_clock::now();
        return std::chrono::duration_cast<std::chrono::microseconds>(
                   wallEnd - wallStart).count();
    }

    uint64_t ticksProcessed() const { return stats_[0].count.load(); }
    uint64_t ordersPlaced()   const { return stats_[1].count.load(); }
    uint64_t tradesExecuted() const { return stats_[2].count.load(); }
    size_t   poolThreads()    const { return pool_.threadCount(); }

private:
    // ── Thread 1: market feed ─────────────────────────────────────────────────
    void feedThread() {
        MarketData data;
        if (!data.loadCSV(csvPath_)) {
            done_.store(true, std::memory_order_release);
            return;
        }
        int count = 0;
        while (data.hasNext() && count < limit_) {
            while (!tickQueue_.push(data.next()))
                std::this_thread::yield();
            stats_[0].count.fetch_add(1, std::memory_order_relaxed);
            count++;
        }
        done_.store(true, std::memory_order_release);
    }

    // ── Thread 2: bot decisions ───────────────────────────────────────────────
    // FIX: Each bot gets its own private staging LOB (one per bot).
    // ThreadPool submits one task per bot — all run concurrently.
    // No shared mutable state between bot tasks — zero data race.
    // After all futures resolve, orders from all staging LOBs are merged
    // and flushed to orderQueue_.
    void botThread() {
        const size_t numBots = bots_.size();
        // One staging LOB per bot — allocated once, reused each tick
        std::vector<LimitOrderBook> stagingLobs(numBots);
        int lastOrderId = Order::nextId.load(std::memory_order_relaxed);

        while (true) {
            if (auto tick = tickQueue_.pop()) {
                lastPrice_ = tick->price;

                // Submit one task per bot — each writes to its own stagingLobs[i]
                std::vector<std::future<void>> futures;
                futures.reserve(numBots);
                for (size_t i = 0; i < numBots; ++i) {
                    futures.push_back(pool_.submit([this, i, &tick, &stagingLobs] {
                        bots_[i]->onPriceUpdate(tick->price, stagingLobs[i], 0);
                    }));
                }
                // Wait for all bots to finish before merging
                for (auto& f : futures) f.wait();

                // Merge orders from all staging LOBs into orderQueue_
                flushAllStagingLobs(stagingLobs, lastOrderId);

            } else {
                if (done_.load(std::memory_order_acquire)) break;
                std::this_thread::yield();
            }
        }
        botDone_.store(true, std::memory_order_release);
    }

    // Drain all staging LOBs, push new orders to orderQueue_, then clear
    void flushAllStagingLobs(std::vector<LimitOrderBook>& stagingLobs,
                              int& lastOrderId) {
        int currentMax = Order::nextId.load(std::memory_order_relaxed);
        if (currentMax == lastOrderId) return;

        for (auto& lob : stagingLobs) {
            for (auto& [price, queue] : lob.bids)
                for (auto& order : queue)
                    if (order.id > lastOrderId && order.quantity > 0) {
                        while (!orderQueue_.push(order)) std::this_thread::yield();
                        stats_[1].count.fetch_add(1, std::memory_order_relaxed);
                    }

            for (auto& [price, queue] : lob.asks)
                for (auto& order : queue)
                    if (order.id > lastOrderId && order.quantity > 0) {
                        while (!orderQueue_.push(order)) std::this_thread::yield();
                        stats_[1].count.fetch_add(1, std::memory_order_relaxed);
                    }

            // Clear staging LOB for next tick — no heap reallocation
            const_cast<LimitOrderBook&>(lob).bids.clear();
            const_cast<LimitOrderBook&>(lob).asks.clear();
            const_cast<LimitOrderBook&>(lob).orderIndex.clear();
        }
        lastOrderId = currentMax;
    }

    // ── Thread 3: matching engine ─────────────────────────────────────────────
    void matchThread(AsyncLogger& logger) {
        LimitOrderBook lob;
        int timestep = 0;

        while (true) {
            if (auto order = orderQueue_.pop()) {
                timestep++;
                lob.addOrder(*order);

                std::string tradeLines;
                matchAndLog(lob, tradeLines, timestep);

                if (!tradeLines.empty()) {
                    logger.log(tradeLines);
                    stats_[2].count.fetch_add(1, std::memory_order_relaxed);
                }
            } else {
                if (botDone_.load(std::memory_order_acquire) && orderQueue_.empty())
                    break;
                std::this_thread::yield();
            }
        }
    }

    // FIX: uses botIndex_ (unordered_map) for O(1) bot lookup — not O(N) scan.
    // No stdout — AsyncLogger handles all I/O on Thread 4.
    void matchAndLog(LimitOrderBook& lob, std::string& out, int timestep) {
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

                out += std::to_string(timestep) + ','
                     + buy.agent  + ','
                     + sell.agent + ','
                     + std::to_string(price) + ','
                     + std::to_string(qty)   + '\n';

                // O(1) lookup — botIndex_ built once in constructor
                auto buyIt2  = botIndex_.find(buy.agent);
                auto sellIt2 = botIndex_.find(sell.agent);
                if (buyIt2  != botIndex_.end()) buyIt2->second->recordTrade(price, qty, true);
                if (sellIt2 != botIndex_.end()) sellIt2->second->recordTrade(price, qty, false);

                buy.quantity  -= qty;
                sell.quantity -= qty;
                if (buy.quantity  == 0) { lob.orderIndex.erase(buy.id);  bidIt->second.pop_front(); }
                if (sell.quantity == 0) { lob.orderIndex.erase(sell.id); askIt->second.pop_front(); }
                if (bidIt->second.empty()) lob.bids.erase(bidIt);
                if (askIt->second.empty()) lob.asks.erase(askIt);
            } else {
                break;
            }
        }
    }

    // ── Members ───────────────────────────────────────────────────────────────
    std::string instrument_;
    std::string csvPath_;
    std::vector<std::unique_ptr<Bot>>& bots_;
    int limit_;

    // O(1) bot lookup — built once in constructor, used in matchAndLog()
    std::unordered_map<std::string, Bot*> botIndex_;

    SPSCQueue<MarketTick, 1024> tickQueue_;
    SPSCQueue<Order, 4096>      orderQueue_;

    std::atomic<bool> done_{false};
    std::atomic<bool> botDone_{false};
    double            lastPrice_{0.0};

    ThreadStats stats_[3];
    ThreadPool  pool_;
};