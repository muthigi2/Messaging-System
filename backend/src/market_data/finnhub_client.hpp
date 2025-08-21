#pragma once

#include <iostream>
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/beast/websocket.hpp>
#include "market_data_types.hpp"

namespace market_data {

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = net::ip::tcp;
using json = nlohmann::json;

class FinnhubClient {
public:
    using DataCallback = std::function<void(const NormalizedMarketData&)>;

    FinnhubClient(const std::string& api_key, 
                 const std::vector<std::string>& symbols,
                 DataCallback callback)
        : api_key_(api_key)
        , symbols_(symbols)
        , callback_(callback)
        , should_continue_(false) {
    }

    void start() {
        should_continue_ = true;
        worker_thread_ = std::thread([this]() {
            try {
                net::io_context ioc;
                websocket::stream<tcp::socket> ws(ioc);

                // Connect to Finnhub WebSocket
                tcp::resolver resolver(ioc);
                auto const results = resolver.resolve("ws.finnhub.io", "443");

                // Connect to the server
                net::connect(ws.next_layer(), results);
                ws.handshake("ws.finnhub.io", "/");

                // Subscribe to symbols
                for (const auto& symbol : symbols_) {
                    json subscribe_msg = {
                        {"type", "subscribe"},
                        {"symbol", symbol}
                    };
                    ws.write(net::buffer(subscribe_msg.dump()));
                }

                // Read messages
                beast::flat_buffer buffer;
                while (should_continue_) {
                    ws.read(buffer);
                    std::string message = beast::buffers_to_string(buffer.data());
                    buffer.consume(buffer.size());

                    try {
                        auto json_msg = json::parse(message);
                        if (json_msg.contains("data")) {
                            for (const auto& data : json_msg["data"]) {
                                NormalizedMarketData market_data;
                                market_data.symbol = data["s"];
                                market_data.price = data["p"];
                                market_data.volume = data["v"];
                                market_data.timestamp = std::chrono::system_clock::now();
                                market_data.source = "FINNHUB";

                                callback_(market_data);
                            }
                        }
                    } catch (const std::exception& e) {
                        std::cerr << "Error parsing message: " << e.what() << std::endl;
                    }
                }

                // Unsubscribe and close
                for (const auto& symbol : symbols_) {
                    json unsubscribe_msg = {
                        {"type", "unsubscribe"},
                        {"symbol", symbol}
                    };
                    ws.write(net::buffer(unsubscribe_msg.dump()));
                }
                ws.close(websocket::close_code::normal);
            } catch (const std::exception& e) {
                std::cerr << "WebSocket error: " << e.what() << std::endl;
            }
        });
    }

    void stop() {
        should_continue_ = false;
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
    }

private:
    std::string api_key_;
    std::vector<std::string> symbols_;
    DataCallback callback_;
    std::atomic<bool> should_continue_;
    std::thread worker_thread_;
};

} // namespace market_data 