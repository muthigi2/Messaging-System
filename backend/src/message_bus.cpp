#include "message_bus.hpp"
#include <iostream>
#include <iomanip>
#include <ctime>
#include <cstring>
#include <stdexcept>

namespace lockfree {

MessageBus::MessageBus(const std::string& name, std::size_t buffer_size)
    : shared_memory_(std::make_unique<SharedMemory>(name, buffer_size)) {
    
    std::cout << "\n=== Creating MessageBus ===" << std::endl;
    
    try {
        // Calculate the size needed for the ring buffer
        const size_t ring_buffer_size = sizeof(RingBuffer<MessageWrapper, DEFAULT_RING_BUFFER_SIZE>);
        std::cout << "Ring buffer size: " << ring_buffer_size << " bytes" << std::endl;
        
        // Create the ring buffer in shared memory
        ring_buffer_ = std::unique_ptr<RingBuffer<MessageWrapper, DEFAULT_RING_BUFFER_SIZE>,
            std::function<void(RingBuffer<MessageWrapper, DEFAULT_RING_BUFFER_SIZE>*)>>(
                new (shared_memory_->get_data()) RingBuffer<MessageWrapper, DEFAULT_RING_BUFFER_SIZE>(),
                [](RingBuffer<MessageWrapper, DEFAULT_RING_BUFFER_SIZE>* ptr) {
                    ptr->~RingBuffer<MessageWrapper, DEFAULT_RING_BUFFER_SIZE>();
                }
            );
        
        std::cout << "Ring buffer created successfully" << std::endl;
        std::cout << "Buffer capacity: " << ring_buffer_->capacity() << " messages" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Failed to create ring buffer: " << e.what() << std::endl;
        throw;
    }
    
    std::cout << "=== MessageBus creation complete ===\n" << std::endl;
}

bool MessageBus::publish(const std::string& topic, const MarketData& data) {
    try {
        MessageWrapper wrapper;
        wrapper.type = MessageType::MARKET_DATA;
        
        // Copy the symbol to fixed-size array
        strncpy(wrapper.data.market_data.symbol, data.symbol, sizeof(wrapper.data.market_data.symbol) - 1);
        wrapper.data.market_data.symbol[sizeof(wrapper.data.market_data.symbol) - 1] = '\0';
        
        wrapper.data.market_data.price = data.price;
        wrapper.data.market_data.volume = data.volume;
        wrapper.data.market_data.seq = sequence_.fetch_add(1, std::memory_order_relaxed) + 1;
        wrapper.data.market_data.timestamp = data.timestamp;
        
        // Copy the source to fixed-size array
        strncpy(wrapper.data.market_data.source, data.source, sizeof(wrapper.data.market_data.source) - 1);
        wrapper.data.market_data.source[sizeof(wrapper.data.market_data.source) - 1] = '\0';
        
        // Try to write to the ring buffer
        if (!ring_buffer_->write(wrapper)) {
            std::cerr << "Failed to write to ring buffer - buffer full" << std::endl;
            dropped_count_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        
        published_count_.fetch_add(1, std::memory_order_relaxed);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error publishing message: " << e.what() << std::endl;
        return false;
    }
}

void MessageBus::process_messages(std::atomic<bool>& should_continue) {
    while (should_continue) {
        try {
            MessageWrapper wrapper;
            if (ring_buffer_->read(wrapper)) {
                if (wrapper.type == MessageType::MARKET_DATA) {
                    // Convert back to MarketData
                    MarketData data;
                    strncpy(data.symbol, wrapper.data.market_data.symbol, sizeof(data.symbol) - 1);
                    data.symbol[sizeof(data.symbol) - 1] = '\0';
                    data.price = wrapper.data.market_data.price;
                    data.volume = wrapper.data.market_data.volume;
                    data.seq = wrapper.data.market_data.seq;
                    data.timestamp = wrapper.data.market_data.timestamp;
                    strncpy(data.source, wrapper.data.market_data.source, sizeof(data.source) - 1);
                    data.source[sizeof(data.source) - 1] = '\0';
                    
                    // Notify all subscribers for this topic
                    auto it = subscribers_.find("market_data");
                    if (it != subscribers_.end()) {
                        for (const auto& callback : it->second) {
                            try {
                                callback(data);
                            } catch (const std::exception& e) {
                                std::cerr << "Error in subscriber callback: " << e.what() << std::endl;
                            }
                        }
                    }
                    processed_count_.fetch_add(1, std::memory_order_relaxed);
                }
            }
            int delay = processing_delay_ms_.load(std::memory_order_relaxed);
            if (delay > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(delay));
            }
        } catch (const std::exception& e) {
            std::cerr << "Error processing messages: " << e.what() << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

MessageBus::~MessageBus() {
    std::cout << "\n=== Destroying MessageBus ===" << std::endl;
    
    try {
        // Destroy the ring buffer first
        if (ring_buffer_) {
            std::cout << "Destroying ring buffer..." << std::endl;
            ring_buffer_.reset();
            std::cout << "Ring buffer destroyed" << std::endl;
        }
        
        // Then destroy the shared memory
        if (shared_memory_) {
            std::cout << "Destroying shared memory..." << std::endl;
            shared_memory_.reset();
            std::cout << "Shared memory destroyed" << std::endl;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error during cleanup: " << e.what() << std::endl;
    }
    
    std::cout << "=== MessageBus destruction complete ===\n" << std::endl;
}

void MessageBus::reset_counters() {
    published_count_.store(0, std::memory_order_relaxed);
    processed_count_.store(0, std::memory_order_relaxed);
    dropped_count_.store(0, std::memory_order_relaxed);
}

} // namespace lockfree 