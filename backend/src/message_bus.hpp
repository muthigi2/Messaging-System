#pragma once

#include <memory>
#include <functional>
#include <unordered_map>
#include <vector>
#include <atomic>
#include <chrono>
#include <thread>
#include <iostream>
#include <cstring>
#include "shared_memory.hpp"
#include "ring_buffer.hpp"

namespace lockfree {

struct MarketData {
    char symbol[16];
    double price;
    double volume;
    uint64_t seq;     // Monotonic sequence id
    int64_t timestamp;  // Store as int64_t instead of time_point
    char source[32];    // Fixed-size array instead of std::string
};

enum class MessageType {
    MARKET_DATA
};

struct MessageWrapper {
    MessageType type;
    union {
        MarketData market_data;
    } data;

    // Default constructor
    MessageWrapper() : type(MessageType::MARKET_DATA) {
        std::memset(&data, 0, sizeof(data));
    }

    // Copy constructor
    MessageWrapper(const MessageWrapper& other) : type(other.type) {
        std::memcpy(&data, &other.data, sizeof(data));
    }

    // Move constructor
    MessageWrapper(MessageWrapper&& other) noexcept : type(other.type) {
        std::memcpy(&data, &other.data, sizeof(data));
    }

    // Copy assignment
    MessageWrapper& operator=(const MessageWrapper& other) {
        if (this != &other) {
            type = other.type;
            std::memcpy(&data, &other.data, sizeof(data));
        }
        return *this;
    }

    // Move assignment
    MessageWrapper& operator=(MessageWrapper&& other) noexcept {
        if (this != &other) {
            type = other.type;
            std::memcpy(&data, &other.data, sizeof(data));
        }
        return *this;
    }
};

class MessageBus {
public:
    static constexpr std::size_t DEFAULT_RING_BUFFER_SIZE = 1024;

    MessageBus(const std::string& name, std::size_t buffer_size = DEFAULT_RING_BUFFER_SIZE);
    ~MessageBus();

    bool publish(const std::string& topic, const MarketData& data);
    // Backward-compatible overload: default to "market_data" topic
    bool publish(const MarketData& data) { return publish("market_data", data); }
    void process_messages(std::atomic<bool>& should_continue);

    template<typename T>
    void subscribe(const std::string& topic, std::function<void(const T&)> callback) {
        if constexpr (std::is_same_v<T, MarketData>) {
            subscribers_[topic].push_back(callback);
        }
    }

    // Backward-compatible overload: pointer-based callback, default topic
    template<typename T>
    void subscribe(std::function<void(const T*)> callback) {
        if constexpr (std::is_same_v<T, MarketData>) {
            subscribers_["market_data"].push_back([callback](const MarketData& data) {
                callback(&data);
            });
        }
    }

    // Test helper methods
    size_t get_read_index() const { return ring_buffer_->get_read_index(); }
    size_t get_write_index() const { return ring_buffer_->get_write_index(); }
    size_t get_size() const { return ring_buffer_->size(); }
    size_t get_capacity() const { return ring_buffer_->capacity(); }
    bool is_full() const { return ring_buffer_->is_full(); }
    bool is_empty() const { return ring_buffer_->is_empty(); }

    // Stats & tuning
    uint64_t get_published_count() const { return published_count_.load(); }
    uint64_t get_processed_count() const { return processed_count_.load(); }
    int get_processing_delay_ms() const { return processing_delay_ms_.load(); }
    void set_processing_delay_ms(int ms) { processing_delay_ms_.store(std::max(0, ms)); }
    uint64_t get_dropped_count() const { return dropped_count_.load(); }
    void reset_counters();

private:
    std::unique_ptr<SharedMemory> shared_memory_;
    std::unique_ptr<RingBuffer<MessageWrapper, DEFAULT_RING_BUFFER_SIZE>, 
                   std::function<void(RingBuffer<MessageWrapper, DEFAULT_RING_BUFFER_SIZE>*)>> ring_buffer_;
    std::unordered_map<std::string, std::vector<std::function<void(const MarketData&)>>> subscribers_;

    // Counters
    std::atomic<uint64_t> published_count_{0};
    std::atomic<uint64_t> processed_count_{0};
    std::atomic<uint64_t> dropped_count_{0};
    // Artificial processing delay to visualize buffer occupancy
    std::atomic<int> processing_delay_ms_{0};
    // Global sequence for messages
    std::atomic<uint64_t> sequence_{0};
};

} // namespace lockfree 