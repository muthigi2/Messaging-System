#pragma once

#include <string>
#include <chrono>
#include <vector>
#include <deque>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace market_data {

// Normalized market data structure
struct NormalizedMarketData {
    std::string symbol;
    double price;
    double volume;
    std::chrono::system_clock::time_point timestamp;
    std::string source;  // e.g., "FINNHUB", "REPLAY"

    // Convert to JSON for WebSocket transmission
    nlohmann::json to_json() const {
        return {
            {"symbol", symbol},
            {"price", price},
            {"volume", volume},
            {"timestamp", std::chrono::system_clock::to_time_t(timestamp)},
            {"source", source}
        };
    }
};

// Circular buffer for storing market data
template<typename T>
class CircularBuffer {
public:
    explicit CircularBuffer(size_t size) : max_size_(size) {}

    void push(const T& item) {
        if (buffer_.size() >= max_size_) {
            buffer_.pop_front();
        }
        buffer_.push_back(item);
    }

    std::vector<T> get_all() const {
        return std::vector<T>(buffer_.begin(), buffer_.end());
    }

    size_t size() const { return buffer_.size(); }
    bool empty() const { return buffer_.empty(); }
    bool full() const { return buffer_.size() >= max_size_; }

private:
    std::deque<T> buffer_;
    size_t max_size_;
};

} // namespace market_data 