#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <cstring>
#include <thread>
#include <chrono>
#include <iostream>
#include <stdexcept>

namespace lockfree {

// Cache line size for alignment
constexpr std::size_t CACHE_LINE_SIZE = 64;

// Align a type to cache line boundary
template<typename T>
struct alignas(CACHE_LINE_SIZE) CacheAligned {
    T value;
};

// Lock-free ring buffer implementation
template<typename T, std::size_t Size>
class RingBuffer {
public:
    static_assert(Size > 0 && ((Size & (Size - 1)) == 0), "Size must be a power of 2");

    RingBuffer() : read_index_(0), write_index_(0) {
        static_assert(alignof(T) <= alignof(std::max_align_t), "Type alignment too large");
    }

    ~RingBuffer() {
        // Call destructors for all elements
        while (read_index_ != write_index_) {
            get(read_index_).~T();
            read_index_ = (read_index_ + 1) & (Size - 1);
        }
    }

    bool write(const T& item) {
        std::size_t next_write = (write_index_ + 1) & (Size - 1);
        if (next_write == read_index_) {
            return false; // Buffer is full
        }

        // Copy construct the item
        new (&get(write_index_)) T(item);
        write_index_ = next_write;
        return true;
    }

    bool read(T& item) {
        if (read_index_ == write_index_) {
            return false; // Buffer is empty
        }

        // Move construct the item
        item = std::move(get(read_index_));
        get(read_index_).~T();
        read_index_ = (read_index_ + 1) & (Size - 1);
        return true;
    }

    std::size_t size() const {
        return (write_index_ - read_index_) & (Size - 1);
    }

    std::size_t capacity() const {
        return Size - 1;
    }

    bool is_full() const {
        return size() == capacity();
    }

    bool is_empty() const {
        return read_index_ == write_index_;
    }

    std::size_t get_read_index() const {
        return read_index_;
    }

    std::size_t get_write_index() const {
        return write_index_;
    }

private:
    T& get(std::size_t index) {
        return *reinterpret_cast<T*>(&buffer_[index * sizeof(T)]);
    }

    const T& get(std::size_t index) const {
        return *reinterpret_cast<const T*>(&buffer_[index * sizeof(T)]);
    }

    alignas(std::max_align_t) char buffer_[Size * sizeof(T)];
    std::atomic<std::size_t> read_index_;
    std::atomic<std::size_t> write_index_;
};

} // namespace lockfree 