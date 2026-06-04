#pragma once
#include "market_data.hpp"
#include <libwebsockets.h>
#include <nlohmann/json.hpp>
#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <iostream>
#include <cstring>

using json = nlohmann::json;

struct FeedState {
    std::function<void(const MarketTick&)> onTick;
    MarketTick current;
    bool isTrade = false;
    bool isDepth = false;
    std::atomic<bool>* running = nullptr;
};

static int feedCallback(struct lws* wsi, enum lws_callback_reasons reason,
                        void* user, void* in, size_t len) {
    FeedState* state = reinterpret_cast<FeedState*>(lws_wsi_user(wsi));
    if (!state) return 0;

    switch (reason) {
        case LWS_CALLBACK_CLIENT_ESTABLISHED:
            std::cout << "[WS] Connected to Binance "
                      << (state->isTrade ? "trade" : "depth") << " stream\n";
            break;

        case LWS_CALLBACK_CLIENT_RECEIVE: {
            std::string msg(reinterpret_cast<char*>(in), len);
            try {
                auto j = json::parse(msg);

                if (state->isTrade && j.contains("p")) {
                    state->current.price     = std::stod(j["p"].get<std::string>());
                    state->current.qty       = std::stod(j["q"].get<std::string>());
                    state->current.timestamp = j.value("T", 0LL);
                    if (state->onTick) state->onTick(state->current);
                }

                if (state->isDepth && j.contains("bids") && j.contains("asks")) {
                    double bidVol = 0.0, askVol = 0.0;
                    for (auto& level : j["bids"])
                        bidVol += std::stod(level[1].get<std::string>());
                    for (auto& level : j["asks"])
                        askVol += std::stod(level[1].get<std::string>());
                    state->current.bidVol = bidVol;
                    state->current.askVol = askVol;
                }

            } catch (const std::exception& e) {
                std::cerr << "[WS] Parse error: " << e.what() << "\n";
            }
            break;
        }

        case LWS_CALLBACK_CLIENT_CLOSED:
            std::cout << "[WS] Disconnected\n";
            break;

        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
            std::cerr << "[WS] Connection error\n";
            break;

        default:
            break;
    }
    return 0;
}

class BinanceWebSocketFeed {
public:
    std::function<void(const MarketTick&)> onTick;

    BinanceWebSocketFeed() : running_(false) {}
    ~BinanceWebSocketFeed() { stop(); }

    void start() {
        running_ = true;
        feedThread_ = std::thread(&BinanceWebSocketFeed::run, this);
    }

    void stop() {
        running_ = false;
        if (feedThread_.joinable()) feedThread_.join();
    }

    bool isRunning() const { return running_.load(); }

private:
    std::atomic<bool> running_;
    std::thread feedThread_;

    void run() {
        struct lws_context_creation_info ctxInfo = {};
        ctxInfo.port      = CONTEXT_PORT_NO_LISTEN;
        ctxInfo.options   = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
        ctxInfo.protocols = protocols_;

        struct lws_context* ctx = lws_create_context(&ctxInfo);
        if (!ctx) { std::cerr << "[WS] Failed to create lws context\n"; return; }

        tradeState_.onTick  = onTick;
        tradeState_.isTrade = true;
        tradeState_.running = &running_;

        depthState_.isDepth = true;
        depthState_.onTick  = nullptr;

        connectStream(ctx, "stream.binance.com", 9443,
                      "/ws/ethusdt@trade", &tradeState_);
        connectStream(ctx, "stream.binance.com", 9443,
                      "/ws/ethusdt@depth5@1000ms", &depthState_);

        while (running_.load()) {
            lws_service(ctx, 50);
        }

        lws_context_destroy(ctx);
    }

    void connectStream(struct lws_context* ctx,
                       const char* host, int port,
                       const char* path, FeedState* state) {
        struct lws_client_connect_info info = {};
        info.context        = ctx;
        info.address        = host;
        info.port           = port;
        info.path           = path;
        info.host           = host;
        info.origin         = host;
        info.ssl_connection = LCCSCF_USE_SSL
                            | LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK
                            | LCCSCF_ALLOW_SELFSIGNED;
        info.protocol       = protocols_[0].name;
        info.userdata       = state;

        struct lws* wsi = lws_client_connect_via_info(&info);
        if (!wsi) std::cerr << "[WS] Failed to connect: " << path << "\n";
    }

    FeedState tradeState_;
    FeedState depthState_;

    static constexpr struct lws_protocols protocols_[] = {
        { "binance-feed", feedCallback, 0, 4096, 0, nullptr, 0 },
        { nullptr, nullptr, 0, 0, 0, nullptr, 0 }
    };
};