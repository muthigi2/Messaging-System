#pragma once

#include "market_data_types.hpp"
#include <vector>
#include <chrono>
#include <functional>
#include <thread>
#include <atomic>
#include <string>

namespace market_data {

class ReplayEngine {
public:
    using MessageCallback = std::function<void(const NormalizedMarketData&)>;

    ReplayEngine(const std::vector<std::string>& symbols, MessageCallback callback);
    ~ReplayEngine();

    void start() {
        should_continue_ = true;
        worker_thread_ = std::thread([this]() {
            while (should_continue_) {
                for (const auto& symbol : symbols_) {
                    NormalizedMarketData data;
                    data.symbol = symbol;
                    data.price = 100.0 + (rand() % 1000) / 10.0;
                    data.volume = rand() % 10000;
                    data.timestamp = std::chrono::system_clock::now();
                    data.source = "REPLAY";

                    callback_(data);
                }
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        });
    }

    void stop() {
        should_continue_ = false;
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
    }

    // Load historical data from a JSON file
    bool load_historical_data(const std::string& filename);
    
    // Start replaying data at specified speed (1.0 = real-time)
    void start_replay(double speed_multiplier = 1.0);
    
    // Stop replay
    void stop_replay();
    
    // Get current replay status
    bool is_replaying() const { return running_; }
    double get_speed() const { return speed_multiplier_; }
    size_t get_total_messages() const { return historical_data_.size(); }
    size_t get_current_index() const { return current_index_; }

private:
    void replay_thread();
    void process_next_message();

    std::vector<NormalizedMarketData> historical_data_;
    MessageCallback callback_;
    std::atomic<bool> running_;
    std::atomic<double> speed_multiplier_;
    size_t current_index_;
    std::thread replay_thread_;
    std::chrono::system_clock::time_point last_message_time_;
    std::vector<std::string> symbols_;
    std::atomic<bool> should_continue_;
    std::thread worker_thread_;
};

} // namespace market_data 