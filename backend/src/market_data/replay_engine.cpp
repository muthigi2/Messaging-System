#include "replay_engine.hpp"
#include <fstream>
#include <iostream>
#include <chrono>
#include <thread>

namespace market_data {

ReplayEngine::ReplayEngine(const std::vector<std::string>& symbols, MessageCallback callback)
    : symbols_(symbols)
    , callback_(callback)
    , running_(false)
    , speed_multiplier_(1.0)
    , current_index_(0) {
}

ReplayEngine::~ReplayEngine() {
    stop_replay();
}

bool ReplayEngine::load_historical_data(const std::string& filename) {
    try {
        std::ifstream file(filename);
        if (!file.is_open()) {
            std::cerr << "Failed to open file: " << filename << std::endl;
            return false;
        }

        json data;
        file >> data;

        historical_data_.clear();
        for (const auto& item : data) {
            NormalizedMarketData market_data;
            market_data.symbol = item["symbol"];
            market_data.price = item["price"];
            market_data.volume = item["volume"];
            int64_t ts = item["timestamp"].get<int64_t>();
            market_data.timestamp = std::chrono::system_clock::from_time_t(ts / 1000);
            market_data.source = "REPLAY";
            historical_data_.push_back(market_data);
        }

        std::cout << "Loaded " << historical_data_.size() << " historical messages" << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error loading historical data: " << e.what() << std::endl;
        return false;
    }
}

void ReplayEngine::start_replay(double speed_multiplier) {
    if (running_) {
        return;
    }

    if (historical_data_.empty()) {
        std::cerr << "No historical data loaded" << std::endl;
        return;
    }

    running_ = true;
    speed_multiplier_ = speed_multiplier;
    current_index_ = 0;
    last_message_time_ = std::chrono::system_clock::now();

    replay_thread_ = std::thread(&ReplayEngine::replay_thread, this);
}

void ReplayEngine::stop_replay() {
    running_ = false;
    if (replay_thread_.joinable()) {
        replay_thread_.join();
    }
}

void ReplayEngine::replay_thread() {
    while (running_ && current_index_ < historical_data_.size()) {
        process_next_message();
    }
    running_ = false;
}

void ReplayEngine::process_next_message() {
    if (current_index_ >= historical_data_.size()) {
        return;
    }

    const auto& message = historical_data_[current_index_];
    
    // Calculate delay based on timestamps and speed multiplier
    if (current_index_ > 0) {
        auto time_diff = message.timestamp - historical_data_[current_index_ - 1].timestamp;
        auto delay = std::chrono::duration_cast<std::chrono::milliseconds>(
            time_diff / speed_multiplier_.load());
        std::this_thread::sleep_for(delay);
    }

    // Send the message
    callback_(message);
    current_index_++;
}

} // namespace market_data 