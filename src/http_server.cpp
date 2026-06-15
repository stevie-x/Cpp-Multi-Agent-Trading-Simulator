#include "httplib.h"
#include "orderbook.hpp"
#include "flat_orderbook.hpp"
#include "order.hpp"
#include <iostream>
#include <string>
#include <mutex>
#include <atomic>
#include <sstream>
#include <limits>

// ── JSON helpers ──────────────────────────────────────────────────────────────
// Minimal hand-rolled parser — avoids pulling in a JSON library.
// Handles the flat key:"value" and key:number shapes the bot fleet sends.

static std::string getStr(const std::string& body, const std::string& key) {
    auto pos = body.find("\"" + key + "\"");
    if (pos == std::string::npos) return "";
    pos = body.find(":", pos);
    pos = body.find("\"", pos);
    auto end = body.find("\"", pos + 1);
    return body.substr(pos + 1, end - pos - 1);
}

static double getDbl(const std::string& body, const std::string& key) {
    auto pos = body.find("\"" + key + "\"");
    if (pos == std::string::npos) return 0.0;
    pos = body.find(":", pos) + 1;
    while (pos < body.size() && body[pos] == ' ') pos++;
    try { return std::stod(body.substr(pos)); }
    catch (...) { return 0.0; }
}

static int getInt(const std::string& body, const std::string& key) {
    return static_cast<int>(getDbl(body, key));
}

// ── Orderbook cache ───────────────────────────────────────────────────────────
// bestBid and bestAsk are cached as atomics so GET /orderbook never needs
// the write mutex. Updated after every successful addOrder / cancelOrder.
// Reads and writes of double are not guaranteed atomic on all platforms, but
// we use std::atomic with the default seq_cst ordering which is sufficient
// for this use case — a slightly stale spread value is harmless.

static std::atomic<double> cachedBestBid{0.0};
static std::atomic<double> cachedBestAsk{0.0};

static void updateCache(const LimitOrderBook& lob) {
    cachedBestBid.store(lob.bids.empty() ? 0.0 : lob.bids.begin()->first,
                        std::memory_order_release);
    cachedBestAsk.store(lob.asks.empty() ? 0.0 : lob.asks.begin()->first,
                        std::memory_order_release);
}

// ── Main ──────────────────────────────────────────────────────────────────────
int main() {
    LimitOrderBook   lob;
    std::mutex       lobMutex;
    std::atomic<int> orderCount{0};

    httplib::Server svr;

    // ── POST /order ───────────────────────────────────────────────────────────
    // Accepts: limit, market, cancel
    // Returns: {"status":"accepted","order_id":N}  on success
    //          {"status":"cancelled"}               on successful cancel
    //          {"status":"not_found"}               if cancel id unknown
    //          {"status":"rejected","reason":"..."}  on bad input
    svr.Post("/order", [&](const httplib::Request& req, httplib::Response& res) {
        const std::string& body = req.body;
        std::string type     = getStr(body, "type");
        std::string side     = getStr(body, "side");
        double      price    = getDbl(body, "price");
        int         quantity = getInt(body, "quantity");
        int         botID    = getInt(body, "bot_id");

        // ── Validation ────────────────────────────────────────────────────────
        // Chaos wave sends zero-price, zero-qty, and other edge cases
        // deliberately. Reject them with a 4xx — do NOT crash or panic.
        if (type != "cancel" && quantity <= 0) {
            res.status = 400;
            res.set_content("{\"status\":\"rejected\",\"reason\":\"quantity must be > 0\"}",
                            "application/json");
            return;
        }
        if (type == "limit" && price <= 0.0) {
            res.status = 400;
            res.set_content("{\"status\":\"rejected\",\"reason\":\"limit price must be > 0\"}",
                            "application/json");
            return;
        }
        if (side != "buy" && side != "sell" && type != "cancel") {
            res.status = 400;
            res.set_content("{\"status\":\"rejected\",\"reason\":\"side must be buy or sell\"}",
                            "application/json");
            return;
        }

        std::string botName = "bot_" + std::to_string(botID);

        // ── Cancel ────────────────────────────────────────────────────────────
        if (type == "cancel") {
            int cancelId = getInt(body, "order_id");
            bool cancelled = false;
            {
                std::lock_guard<std::mutex> lock(lobMutex);
                cancelled = lob.cancelOrder(cancelId);
                if (cancelled) updateCache(lob);
            }
            // Return correct status — correctness check uses this
            res.set_content(cancelled
                ? "{\"status\":\"cancelled\"}"
                : "{\"status\":\"not_found\"}",
                "application/json");
            return;
        }

        // ── Limit order ───────────────────────────────────────────────────────
        int orderId = ++orderCount;
        if (type == "limit") {
            Side s = (side == "buy") ? Side::BUY : Side::SELL;
            {
                std::lock_guard<std::mutex> lock(lobMutex);
                lob.addOrder(Order::makeLimitOrder(botName, price, quantity, s));
                updateCache(lob);
            }
            res.set_content(
                "{\"status\":\"accepted\",\"order_id\":" + std::to_string(orderId) + "}",
                "application/json");
            return;
        }

        // ── Market order ──────────────────────────────────────────────────────
        // Implemented as a limit order at an extreme price to guarantee
        // immediate fill against whatever is resting in the book.
        if (type == "market") {
            Side   s           = (side == "buy") ? Side::BUY : Side::SELL;
            double marketPrice = (s == Side::BUY)
                ? std::numeric_limits<double>::max()
                : 0.0;
            {
                std::lock_guard<std::mutex> lock(lobMutex);
                lob.addOrder(Order::makeLimitOrder(botName, marketPrice, quantity, s));
                updateCache(lob);
            }
            res.set_content(
                "{\"status\":\"accepted\",\"order_id\":" + std::to_string(orderId) + "}",
                "application/json");
            return;
        }

        // ── Unknown type ──────────────────────────────────────────────────────
        res.status = 400;
        res.set_content("{\"status\":\"rejected\",\"reason\":\"unknown order type\"}",
                        "application/json");
    });

    // ── GET /orderbook ────────────────────────────────────────────────────────
    // Returns best bid, best ask, and spread.
    // Reads from atomics — no mutex needed, never blocks writers.
    svr.Get("/orderbook", [&](const httplib::Request&, httplib::Response& res) {
        double bid    = cachedBestBid.load(std::memory_order_acquire);
        double ask    = cachedBestAsk.load(std::memory_order_acquire);
        double spread = (ask > 0.0 && bid > 0.0) ? ask - bid : 0.0;

        std::ostringstream oss;
        oss << std::fixed;
        oss << "{\"best_bid\":"  << bid
            << ",\"best_ask\":"  << ask
            << ",\"spread\":"    << spread << "}";

        res.set_content(oss.str(), "application/json");
    });

    // ── GET /orderbook/depth ──────────────────────────────────────────────────
    // Returns top 5 levels on each side. Used by the bot fleet's optional
    // depth-scoring check. Harmless no-op for the hackathon if unused.
    svr.Get("/orderbook/depth", [&](const httplib::Request&, httplib::Response& res) {
        std::ostringstream oss;
        oss << std::fixed;
        oss << "{\"bids\":[";
        {
            std::lock_guard<std::mutex> lock(lobMutex);
            int count = 0;
            for (auto& [price, queue] : lob.bids) {
                if (count++ >= 5) break;
                int qty = 0;
                for (auto& o : queue) qty += o.quantity;
                if (count > 1) oss << ",";
                oss << "{\"price\":" << price << ",\"qty\":" << qty << "}";
            }
            oss << "],\"asks\":[";
            count = 0;
            for (auto& [price, queue] : lob.asks) {
                if (count++ >= 5) break;
                int qty = 0;
                for (auto& o : queue) qty += o.quantity;
                if (count > 1) oss << ",";
                oss << "{\"price\":" << price << ",\"qty\":" << qty << "}";
            }
        }
        oss << "]}";
        res.set_content(oss.str(), "application/json");
    });

    std::cout << "C++ Trading Engine HTTP Server running on :8080\n";
    std::cout << "Endpoints: POST /order  GET /orderbook  GET /orderbook/depth\n";
    svr.listen("0.0.0.0", 8080);
    return 0;
}