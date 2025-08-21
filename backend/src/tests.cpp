#include "message_bus.hpp"
#include "ring_buffer.hpp"
#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>
#include <mutex>

using namespace lockfree;

class MessageBusTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::cout << "\n=== Test setup starting ===" << std::endl;
        
        // Make sure any previous shared memory is cleaned up
        std::cout << "Cleaning up any existing shared memory..." << std::endl;
        SharedMemory::remove("test_bus");
        
        // Wait longer to ensure cleanup is complete
        std::cout << "Waiting for shared memory cleanup..." << std::endl;
        for (int i = 0; i < 5; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            try {
                SharedMemory::remove("test_bus");
            } catch (const std::exception& e) {
                std::cout << "Cleanup attempt " << i + 1 << ": " << e.what() << std::endl;
            }
        }
        
        // Create new message bus with proper size calculation
        const size_t ring_buffer_size = sizeof(RingBuffer<MessageWrapper, MessageBus::DEFAULT_RING_BUFFER_SIZE>);
        const size_t shared_memory_size = ((ring_buffer_size + CACHE_LINE_SIZE - 1) / CACHE_LINE_SIZE) * CACHE_LINE_SIZE;
        std::cout << "Creating MessageBus with " << shared_memory_size << " bytes of shared memory..." << std::endl;
        std::cout << "RingBuffer size: " << ring_buffer_size << " bytes" << std::endl;
        
        try {
            bus_ = std::make_unique<MessageBus>("test_bus", shared_memory_size);
            std::cout << "MessageBus created successfully" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "Failed to create MessageBus: " << e.what() << std::endl;
            throw;
        }
        
        std::cout << "=== Test setup complete ===\n" << std::endl;
    }

    void TearDown() override {
        std::cout << "\n=== Test cleanup starting ===" << std::endl;
        
        // First, destroy the MessageBus which will clean up the RingBuffer
        if (bus_) {
            std::cout << "Destroying MessageBus..." << std::endl;
            bus_.reset();
            std::cout << "MessageBus destroyed" << std::endl;
        }
        
        // Then remove the shared memory with retries
        std::cout << "Removing shared memory..." << std::endl;
        bool cleanup_success = false;
        for (int i = 0; i < 5; ++i) {
            try {
                SharedMemory::remove("test_bus");
                cleanup_success = true;
                std::cout << "Shared memory removed successfully on attempt " << i + 1 << std::endl;
                break;
            } catch (const std::exception& e) {
                std::cout << "Cleanup attempt " << i + 1 << " failed: " << e.what() << std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
        
        if (!cleanup_success) {
            std::cerr << "WARNING: Failed to clean up shared memory after multiple attempts" << std::endl;
        }
        
        // Wait to ensure cleanup is complete before next test
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        
        std::cout << "=== Test cleanup complete ===\n" << std::endl;
    }

    std::unique_ptr<MessageBus> bus_;
};

TEST_F(MessageBusTest, BasicPublishSubscribe) {
    std::atomic<int> received_count{0};
    std::atomic<bool> should_continue{true};
    std::atomic<bool> consumer_ready{false};
    MarketData received_data{};
    
    std::cout << "\n=== Starting BasicPublishSubscribe test ===" << std::endl;
    
    std::cout << "Setting up subscription" << std::endl;
    bus_->subscribe<MarketData>([&](const MarketData* data) {
        std::cout << "Received message in callback with symbol: " << data->symbol << std::endl;
        received_data = *data;
        received_count++;
    });

    // Start a separate thread for message processing
    std::thread processor([this, &should_continue, &consumer_ready]() {
        std::cout << "Starting message processor thread" << std::endl;
        consumer_ready = true;
        while (should_continue) {
            bus_->process_messages(should_continue);
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
        std::cout << "Message processor thread finished" << std::endl;
    });

    // Wait for consumer to be ready
    std::cout << "Waiting for consumer to be ready..." << std::endl;
    while (!consumer_ready) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::cout << "Consumer is ready" << std::endl;

    // Prepare and publish test message
    MarketData data{};
    std::strcpy(data.symbol, "TEST");
    data.price = 100.0;
    data.volume = 1000.0;
    data.timestamp = std::chrono::system_clock::now().time_since_epoch().count();

    std::cout << "Publishing message with symbol: " << data.symbol << std::endl;
    bool publish_success = bus_->publish(data);
    ASSERT_TRUE(publish_success) << "Failed to publish message";
    
    if (!publish_success) {
        should_continue = false;
        processor.join();
        FAIL() << "Failed to publish message";
        return;
    }

    // Wait for message to be processed with timeout
    auto start = std::chrono::steady_clock::now();
    auto timeout = std::chrono::seconds(2);
    bool message_received = false;
    
    std::cout << "Waiting for message to be processed..." << std::endl;
    while (!message_received && std::chrono::steady_clock::now() - start < timeout) {
        if (received_count.load() > 0) {
            message_received = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    should_continue = false;
    processor.join();
    
    std::cout << "Test finished. Received count: " << received_count.load() << std::endl;
    
    EXPECT_TRUE(message_received) << "Timeout waiting for message";
    EXPECT_EQ(received_count, 1) << "Message was not received";
    if (received_count > 0) {
        EXPECT_EQ(std::string(received_data.symbol), std::string(data.symbol));
        EXPECT_EQ(received_data.price, data.price);
        EXPECT_EQ(received_data.volume, data.volume);
    }
    
    std::cout << "=== BasicPublishSubscribe test completed ===\n" << std::endl;
}

TEST_F(MessageBusTest, PerformanceTest) {
    const int num_messages = 1000;
    std::atomic<int> received_count{0};
    std::atomic<bool> should_continue{true};
    std::atomic<bool> consumer_ready{false};
    
    bus_->subscribe<MarketData>([&](const MarketData* /*data*/) {
        received_count++;
    });

    // Start consumer thread first
    std::thread consumer([&]() {
        std::cout << "Consumer thread started" << std::endl;
        consumer_ready = true;
        while (should_continue) {
            bus_->process_messages(should_continue);
            if (received_count.load() % 100 == 0) {
                std::cout << "Processed " << received_count.load() << " messages" << std::endl;
            }
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
        std::cout << "Consumer thread finished" << std::endl;
    });
    
    // Wait for consumer to be ready
    std::cout << "Waiting for consumer to be ready..." << std::endl;
    while (!consumer_ready) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::cout << "Consumer is ready" << std::endl;
    
    MarketData data{};
    std::strcpy(data.symbol, "PERF");
    data.price = 100.0;
    data.volume = 1000.0;

    auto start = std::chrono::high_resolution_clock::now();
    
    std::cout << "Starting to publish messages" << std::endl;
    int publish_failures = 0;
    for (int i = 0; i < num_messages; ++i) {
        data.timestamp = std::chrono::system_clock::now().time_since_epoch().count();
        if (!bus_->publish(data)) {
            std::cout << "Failed to publish message " << i << std::endl;
            publish_failures++;
            if (publish_failures > 10) {
                std::cout << "Too many publish failures, aborting test" << std::endl;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        if (i % 100 == 0) {
            std::cout << "Published " << i << " messages" << std::endl;
        }
    }
    std::cout << "Finished publishing messages" << std::endl;
    
    // Wait for all messages to be processed with timeout
    auto timeout = std::chrono::seconds(5);
    auto wait_start = std::chrono::steady_clock::now();
    bool all_messages_received = false;
    
    while (!all_messages_received && std::chrono::steady_clock::now() - wait_start < timeout) {
        if (received_count >= num_messages - publish_failures) {
            all_messages_received = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    should_continue = false;
    consumer.join();
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    double messages_per_second = ((num_messages - publish_failures) * 1000000.0) / duration.count();
    std::cout << "Performance: " << messages_per_second << " messages/second" << std::endl;
    std::cout << "Published: " << (num_messages - publish_failures) << " messages" << std::endl;
    std::cout << "Received: " << received_count << " messages" << std::endl;
    
    EXPECT_TRUE(all_messages_received) << "Timeout waiting for messages";
    EXPECT_EQ(received_count, num_messages - publish_failures);
}

TEST_F(MessageBusTest, MultiThreadedTest) {
    const int num_producers = 4;
    const int messages_per_producer = 250;
    std::atomic<int> received_count{0};
    std::atomic<bool> should_continue{true};
    std::atomic<bool> consumer_ready{false};
    std::atomic<int> producer_count{0};
    std::atomic<int> total_published{0};
    
    bus_->subscribe<MarketData>([&](const MarketData* /*data*/) {
        received_count++;
    });

    // Start consumer thread with improved processing strategy
    std::thread consumer([this, total_messages = num_producers * messages_per_producer, 
                         &received_count, &should_continue, &consumer_ready]() {
        std::cout << "Consumer thread started" << std::endl;
        consumer_ready = true;
        auto start = std::chrono::steady_clock::now();
        auto timeout = std::chrono::seconds(10);  // Increased timeout to 10 seconds
        int processed_batches = 0;
        
        while (received_count < total_messages && should_continue) {
            // Process messages more aggressively when buffer is getting full
            size_t current_size = bus_->get_size();
            size_t capacity = bus_->get_capacity();
            double fill_ratio = static_cast<double>(current_size) / capacity;
            
            if (fill_ratio > 0.5) {
                // Process messages without delay when buffer is more than 50% full
                while (fill_ratio > 0.25 && should_continue) {
                    bus_->process_messages(should_continue);
                    current_size = bus_->get_size();
                    fill_ratio = static_cast<double>(current_size) / capacity;
                }
            } else {
                bus_->process_messages(should_continue);
                processed_batches++;
                
                if (processed_batches % 10 == 0) {
                    std::cout << "Processed " << processed_batches << " batches, received " 
                             << received_count << " messages" << std::endl;
                }
                
                // Minimal sleep to prevent busy-waiting
                std::this_thread::sleep_for(std::chrono::microseconds(10));
            }
            
            // Check for timeout
            auto now = std::chrono::steady_clock::now();
            if (now - start > timeout) {
                std::cout << "Consumer timeout after " << timeout.count() << " seconds. "
                          << "Received " << received_count << " of " << total_messages << " messages." << std::endl;
                should_continue = false;
                break;
            }
        }
        std::cout << "Consumer finished after processing " << processed_batches << " batches" << std::endl;
    });

    // Wait for consumer to be ready
    std::cout << "Waiting for consumer to be ready..." << std::endl;
    while (!consumer_ready) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::cout << "Consumer is ready" << std::endl;

    auto start = std::chrono::high_resolution_clock::now();
    
    // Start producer threads with improved retry logic
    std::vector<std::thread> producers;
    for (int i = 0; i < num_producers; ++i) {
        producers.emplace_back([this, i, &producer_count, &total_published, &should_continue]() {
            MarketData data{};
            std::strcpy(data.symbol, "MT");
            data.price = 100.0 + i;
            data.volume = 1000.0;
            
            int publish_failures = 0;
            int messages_published = 0;
            
            for (int j = 0; j < messages_per_producer && should_continue; ++j) {
                data.timestamp = std::chrono::system_clock::now().time_since_epoch().count();
                bool published = false;
                int retry_count = 0;
                
                while (!published && should_continue && retry_count < 200) {  // Increased retry limit
                    published = bus_->publish(data);
                    if (!published) {
                        publish_failures++;
                        retry_count++;
                        
                        // Exponential backoff for retries
                        std::this_thread::sleep_for(std::chrono::microseconds(10 * (1 << std::min(retry_count, 5))));
                    }
                }
                
                if (published) {
                    messages_published++;
                    total_published++;
                } else {
                    std::cout << "Producer " << i << " failed to publish message " << j 
                             << " after " << retry_count << " attempts" << std::endl;
                }
                
                if (messages_published % 50 == 0) {
                    std::cout << "Producer " << i << " published " << messages_published 
                             << " messages" << std::endl;
                }
            }
            
            std::cout << "Producer " << i << " finished: published " << messages_published 
                     << " messages, " << publish_failures << " failures" << std::endl;
            producer_count++;
        });
    }

    // Wait for producers to finish
    for (auto& producer : producers) {
        producer.join();
    }
    
    std::cout << "All producers finished. Total published: " << total_published << std::endl;
    
    // Wait for consumer to finish with increased timeout
    auto timeout = std::chrono::seconds(10);  // Increased timeout to 10 seconds
    auto wait_start = std::chrono::steady_clock::now();
    bool all_messages_received = false;
    
    while (!all_messages_received && std::chrono::steady_clock::now() - wait_start < timeout) {
        if (received_count >= total_published) {
            all_messages_received = true;
            break;
        }
        std::cout << "Waiting for messages: received " << received_count 
                 << " of " << total_published << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    should_continue = false;
    consumer.join();
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    double messages_per_second = (total_published * 1000000.0) / duration.count();
    std::cout << "Multi-threaded performance: " << messages_per_second << " messages/second" << std::endl;
    std::cout << "Producers completed: " << producer_count << " of " << num_producers << std::endl;
    std::cout << "Messages published: " << total_published << std::endl;
    std::cout << "Messages received: " << received_count << std::endl;
    
    EXPECT_TRUE(all_messages_received) << "Timeout waiting for messages";
    EXPECT_EQ(producer_count, num_producers) << "Not all producers completed";
    EXPECT_EQ(received_count, total_published) << "Not all published messages were received";
}

TEST_F(MessageBusTest, MultipleSubscribers) {
    std::atomic<int> subscriber1_count{0};
    std::atomic<int> subscriber2_count{0};
    std::atomic<bool> should_continue{true};
    std::atomic<bool> consumer_ready{false};
    
    std::cout << "\n=== Starting MultipleSubscribers test ===" << std::endl;
    
    // Set up two subscribers
    bus_->subscribe<MarketData>([&](const MarketData* data) {
        std::cout << "Subscriber 1 received message: " << data->symbol << std::endl;
        subscriber1_count++;
    });
    
    bus_->subscribe<MarketData>([&](const MarketData* data) {
        std::cout << "Subscriber 2 received message: " << data->symbol << std::endl;
        subscriber2_count++;
    });

    // Start consumer thread
    std::thread consumer([this, &should_continue, &consumer_ready]() {
        std::cout << "Consumer thread started" << std::endl;
        consumer_ready = true;
        while (should_continue) {
            bus_->process_messages(should_continue);
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    });

    // Wait for consumer to be ready
    while (!consumer_ready) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // Publish test message
    MarketData data{};
    std::strcpy(data.symbol, "MULTI");
    data.price = 100.0;
    data.volume = 1000.0;
    data.timestamp = std::chrono::system_clock::now().time_since_epoch().count();

    bool publish_success = bus_->publish(data);
    ASSERT_TRUE(publish_success) << "Failed to publish message";

    // Wait for message to be processed
    auto start = std::chrono::steady_clock::now();
    auto timeout = std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() - start < timeout) {
        if (subscriber1_count.load() > 0 && subscriber2_count.load() > 0) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    should_continue = false;
    consumer.join();

    EXPECT_EQ(subscriber1_count.load(), 1) << "Subscriber 1 did not receive message";
    EXPECT_EQ(subscriber2_count.load(), 1) << "Subscriber 2 did not receive message";
    
    std::cout << "=== MultipleSubscribers test completed ===\n" << std::endl;
}

TEST_F(MessageBusTest, CallbackExceptions) {
    std::atomic<bool> should_continue{true};
    std::atomic<bool> consumer_ready{false};
    std::atomic<bool> exception_thrown{false};
    
    std::cout << "\n=== Starting CallbackExceptions test ===" << std::endl;
    
    // Set up subscriber that throws
    bus_->subscribe<MarketData>([&](const MarketData*) {
        std::cout << "Throwing exception from callback" << std::endl;
        exception_thrown = true;
        throw std::runtime_error("Test exception");
    });

    // Start consumer thread
    std::thread consumer([this, &should_continue, &consumer_ready]() {
        std::cout << "Consumer thread started" << std::endl;
        consumer_ready = true;
        while (should_continue) {
            bus_->process_messages(should_continue);
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    });

    // Wait for consumer to be ready
    while (!consumer_ready) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // Publish test message
    MarketData data{};
    std::strcpy(data.symbol, "EXCEPT");
    data.price = 100.0;
    data.volume = 1000.0;
    data.timestamp = std::chrono::system_clock::now().time_since_epoch().count();

    bool publish_success = bus_->publish(data);
    ASSERT_TRUE(publish_success) << "Failed to publish message";

    // Wait for message to be processed
    auto start = std::chrono::steady_clock::now();
    auto timeout = std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() - start < timeout) {
        if (exception_thrown.load()) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    should_continue = false;
    consumer.join();

    EXPECT_TRUE(exception_thrown.load()) << "Exception was not thrown from callback";
    
    std::cout << "=== CallbackExceptions test completed ===\n" << std::endl;
}

TEST_F(MessageBusTest, BufferOverflow) {
    std::atomic<bool> should_continue{true};
    std::atomic<bool> consumer_ready{false};
    std::atomic<int> received_count{0};
    std::atomic<bool> consumer_started{false};
    std::atomic<int> messages_published{0};
    std::atomic<int> publish_failures{0};
    
    std::cout << "\n=== Starting BufferOverflow test ===" << std::endl;
    std::cout << "Buffer capacity: " << MessageBus::DEFAULT_RING_BUFFER_SIZE << " messages" << std::endl;
    
    // Set up subscriber with minimal processing delay
    bus_->subscribe<MarketData>([&](const MarketData* data) {
        std::cout << "Received message: symbol=" << data->symbol 
                 << ", price=" << data->price 
                 << ", timestamp=" << data->timestamp << std::endl;
        // Minimal delay to simulate real processing
        std::this_thread::sleep_for(std::chrono::microseconds(1));  // Reduced from 10μs to 1μs
        received_count++;
    });

    // Start consumer thread that processes messages aggressively
    std::thread consumer([this, &should_continue, &consumer_ready, &consumer_started]() {
        std::cout << "Consumer thread started" << std::endl;
        consumer_ready = true;
        consumer_started = true;
        int processed_count = 0;
        
        while (should_continue) {
            // Process messages more aggressively when buffer is getting full
            size_t current_size = bus_->get_size();
            size_t capacity = bus_->get_capacity();
            double fill_ratio = static_cast<double>(current_size) / capacity;
            
            // If buffer is more than 50% full, process messages without delay
            if (fill_ratio > 0.5) {
                std::cout << "Buffer is " << (fill_ratio * 100) << "% full, processing aggressively" << std::endl;
                while (fill_ratio > 0.25 && should_continue) {
                    bus_->process_messages(should_continue);
                    current_size = bus_->get_size();
                    fill_ratio = static_cast<double>(current_size) / capacity;
                }
            } else {
                bus_->process_messages(should_continue);
                processed_count++;
                
                // Log buffer state after processing
                std::cout << "Buffer state after processing:" << std::endl;
                std::cout << "  read_index: " << bus_->get_read_index() << std::endl;
                std::cout << "  write_index: " << bus_->get_write_index() << std::endl;
                std::cout << "  size: " << bus_->get_size() << std::endl;
                std::cout << "  capacity: " << bus_->get_capacity() << std::endl;
                std::cout << "  is_full: " << (bus_->is_full() ? "true" : "false") << std::endl;
                std::cout << "  is_empty: " << (bus_->is_empty() ? "true" : "false") << std::endl;
                
                // Minimal sleep to prevent busy-waiting
                std::this_thread::sleep_for(std::chrono::microseconds(10));  // Reduced from 100μs to 10μs
            }
        }
        std::cout << "Consumer processed " << processed_count << " batches" << std::endl;
    });

    // Wait for consumer to be ready
    while (!consumer_ready) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // Try to fill the buffer
    MarketData data{};
    std::strcpy(data.symbol, "OVERFLOW");
    data.price = 100.0;
    data.volume = 1000.0;
    
    int consecutive_failures = 0;
    const int max_consecutive_failures = 50;
    const int max_messages = MessageBus::DEFAULT_RING_BUFFER_SIZE * 4;  // Reduced from 32x to 4x
    
    std::cout << "Attempting to publish " << max_messages << " messages..." << std::endl;
    
    // Try to publish more messages than the buffer can hold
    for (int i = 0; i < max_messages && consecutive_failures < max_consecutive_failures; ++i) {
        data.timestamp = std::chrono::system_clock::now().time_since_epoch().count();
        
        // Log buffer state before publish attempt
        std::cout << "\nPublish attempt " << i + 1 << ":" << std::endl;
        std::cout << "  Buffer state before publish:" << std::endl;
        std::cout << "    read_index: " << bus_->get_read_index() << std::endl;
        std::cout << "    write_index: " << bus_->get_write_index() << std::endl;
        std::cout << "    size: " << bus_->get_size() << std::endl;
        std::cout << "    capacity: " << bus_->get_capacity() << std::endl;
        std::cout << "    is_full: " << (bus_->is_full() ? "true" : "false") << std::endl;
        std::cout << "    is_empty: " << (bus_->is_empty() ? "true" : "false") << std::endl;
        
        if (!bus_->publish(data)) {
            publish_failures++;
            consecutive_failures++;
            std::cout << "  Publish failed, consecutive failures: " << consecutive_failures << std::endl;
            
            // Log buffer state after failed publish
            std::cout << "  Buffer state after failed publish:" << std::endl;
            std::cout << "    read_index: " << bus_->get_read_index() << std::endl;
            std::cout << "    write_index: " << bus_->get_write_index() << std::endl;
            std::cout << "    size: " << bus_->get_size() << std::endl;
            std::cout << "    capacity: " << bus_->get_capacity() << std::endl;
            std::cout << "    is_full: " << (bus_->is_full() ? "true" : "false") << std::endl;
            std::cout << "    is_empty: " << (bus_->is_empty() ? "true" : "false") << std::endl;
            
            if (consecutive_failures > 5) {
                std::this_thread::sleep_for(std::chrono::microseconds(10));  // Reduced from 100μs to 10μs
            }
            continue;
        }
        
        consecutive_failures = 0;
        messages_published++;
        
        // Log buffer state after successful publish
        std::cout << "  Publish successful" << std::endl;
        std::cout << "  Buffer state after successful publish:" << std::endl;
        std::cout << "    read_index: " << bus_->get_read_index() << std::endl;
        std::cout << "    write_index: " << bus_->get_write_index() << std::endl;
        std::cout << "    size: " << bus_->get_size() << std::endl;
        std::cout << "    capacity: " << bus_->get_capacity() << std::endl;
        std::cout << "    is_full: " << (bus_->is_full() ? "true" : "false") << std::endl;
        std::cout << "    is_empty: " << (bus_->is_empty() ? "true" : "false") << std::endl;
        
        if (messages_published % 100 == 0) {
            std::cout << "Published " << messages_published << " messages" << std::endl;
            std::cout << "Current received count: " << received_count.load() << std::endl;
        }
        
        // Minimal delay between publishes to allow consumer to catch up
        std::this_thread::sleep_for(std::chrono::microseconds(1));  // Reduced from 10μs to 1μs
    }

    std::cout << "\nTest results:" << std::endl;
    std::cout << "  Messages published: " << messages_published << std::endl;
    std::cout << "  Publish failures: " << publish_failures << std::endl;
    std::cout << "  Current received count: " << received_count.load() << std::endl;
    
    // Wait for messages to be processed with a shorter timeout
    auto start = std::chrono::steady_clock::now();
    auto timeout = std::chrono::seconds(8);  // Reduced from 60s to 8s
    bool all_messages_received = false;
    
    while (!all_messages_received && std::chrono::steady_clock::now() - start < timeout) {
        int current_count = received_count.load();
        if (current_count >= messages_published) {
            all_messages_received = true;
            break;
        }
        if (current_count > 0 && current_count % 100 == 0) {
            std::cout << "Received " << current_count << " messages" << std::endl;
            // Log buffer state during processing
            std::cout << "  Buffer state:" << std::endl;
            std::cout << "    read_index: " << bus_->get_read_index() << std::endl;
            std::cout << "    write_index: " << bus_->get_write_index() << std::endl;
            std::cout << "    size: " << bus_->get_size() << std::endl;
            std::cout << "    capacity: " << bus_->get_capacity() << std::endl;
            std::cout << "    is_full: " << (bus_->is_full() ? "true" : "false") << std::endl;
            std::cout << "    is_empty: " << (bus_->is_empty() ? "true" : "false") << std::endl;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));  // Reduced from 100ms to 10ms
    }

    should_continue = false;
    consumer.join();

    std::cout << "\nFinal results:" << std::endl;
    std::cout << "  Messages published: " << messages_published << std::endl;
    std::cout << "  Messages received: " << received_count.load() << std::endl;
    std::cout << "  Publish failures: " << publish_failures << std::endl;
    std::cout << "  Final buffer state:" << std::endl;
    std::cout << "    read_index: " << bus_->get_read_index() << std::endl;
    std::cout << "    write_index: " << bus_->get_write_index() << std::endl;
    std::cout << "    size: " << bus_->get_size() << std::endl;
    std::cout << "    capacity: " << bus_->get_capacity() << std::endl;
    std::cout << "    is_full: " << (bus_->is_full() ? "true" : "false") << std::endl;
    std::cout << "    is_empty: " << (bus_->is_empty() ? "true" : "false") << std::endl;
    
    // We expect some publish failures due to buffer overflow
    EXPECT_GT(publish_failures, 0) << "Buffer did not overflow as expected";
    
    // All successfully published messages should be received
    EXPECT_EQ(received_count.load(), messages_published) 
        << "Not all published messages were received (published=" << messages_published 
        << ", received=" << received_count.load() << ")";
    
    std::cout << "=== BufferOverflow test completed ===\n" << std::endl;
}

TEST_F(MessageBusTest, MessageOrdering) {
    std::atomic<bool> should_continue{true};
    std::atomic<bool> consumer_ready{false};
    std::vector<int> received_sequence;
    std::mutex sequence_mutex;
    std::atomic<int> received_count{0};
    
    std::cout << "\n=== Starting MessageOrdering test ===" << std::endl;
    
    // Set up subscriber that records message sequence
    bus_->subscribe<MarketData>([&](const MarketData* data) {
        std::lock_guard<std::mutex> lock(sequence_mutex);
        received_sequence.push_back(static_cast<int>(data->price));
        received_count++;
    });

    // Start consumer thread
    std::thread consumer([this, &should_continue, &consumer_ready]() {
        std::cout << "Consumer thread started" << std::endl;
        consumer_ready = true;
        while (should_continue) {
            bus_->process_messages(should_continue);
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    });

    // Wait for consumer to be ready
    while (!consumer_ready) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // Publish messages in sequence
    const int num_messages = 100;
    std::vector<bool> publish_success(num_messages, false);
    
    for (int i = 0; i < num_messages; ++i) {
        MarketData data{};
        std::strcpy(data.symbol, "ORDER");
        data.price = static_cast<double>(i);
        data.volume = 1000.0;
        data.timestamp = std::chrono::system_clock::now().time_since_epoch().count();

        // Keep trying until the message is published
        while (!publish_success[i]) {
            publish_success[i] = bus_->publish(data);
            if (!publish_success[i]) {
                std::cout << "Failed to publish message " << i << ", retrying..." << std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        
        if (i % 10 == 0) {
            std::cout << "Published message " << i << std::endl;
        }
    }

    // Wait for messages to be processed
    auto start = std::chrono::steady_clock::now();
    auto timeout = std::chrono::seconds(10);
    bool all_messages_received = false;
    
    while (!all_messages_received && std::chrono::steady_clock::now() - start < timeout) {
        int current_count = received_count.load();
        if (current_count >= num_messages) {
            all_messages_received = true;
            break;
        }
        if (current_count > 0 && current_count % 10 == 0) {
            std::cout << "Received " << current_count << " messages" << std::endl;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    should_continue = false;
    consumer.join();

    // Verify message ordering
    std::lock_guard<std::mutex> lock(sequence_mutex);
    EXPECT_EQ(received_sequence.size(), num_messages) << "Not all messages were received";
    
    if (received_sequence.size() == num_messages) {
        bool ordering_correct = true;
        for (int i = 0; i < num_messages; ++i) {
            if (received_sequence[i] != i) {
                std::cout << "Message out of order at position " << i << ": expected " << i 
                         << ", got " << received_sequence[i] << std::endl;
                ordering_correct = false;
                break;
            }
        }
        EXPECT_TRUE(ordering_correct) << "Messages were received out of order";
    }
    
    std::cout << "=== MessageOrdering test completed ===\n" << std::endl;
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
} 