#pragma once

#include <string>
#include <memory>
#include <stdexcept>

namespace lockfree {

class SharedMemory {
public:
    SharedMemory(const std::string& name, std::size_t size);
    ~SharedMemory();

    void* get_data() const { return data_; }
    std::size_t get_size() const { return size_; }

    static void remove(const std::string& name);

private:
    std::string name_;
    std::size_t size_;
    void* data_;
    int fd_;
};

} // namespace lockfree 