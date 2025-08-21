#include "shared_memory.hpp"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdexcept>
#include <iostream>
#include <cerrno>
#include <cstring>
#include <atomic>

namespace lockfree {

SharedMemory::SharedMemory(const std::string& name, std::size_t size)
    : name_("/" + name), size_(size), data_(nullptr), fd_(-1) {
    
    std::cout << "\n=== Creating SharedMemory ===" << std::endl;
    std::cout << "Name: " << name_ << std::endl;
    std::cout << "Size: " << size_ << " bytes" << std::endl;
    
    try {
        // Try to remove existing shared memory first
        shm_unlink(name_.c_str());
        
        // Create shared memory object
        fd_ = shm_open(name_.c_str(), O_CREAT | O_RDWR, 0666);
        if (fd_ == -1) {
            throw std::runtime_error("Failed to create shared memory: " + std::string(strerror(errno)));
        }
        
        // Set size
        if (ftruncate(fd_, size_) == -1) {
            close(fd_);
            throw std::runtime_error("Failed to set shared memory size: " + std::string(strerror(errno)));
        }
        
        // Map memory
        data_ = mmap(nullptr, size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
        if (data_ == MAP_FAILED) {
            close(fd_);
            throw std::runtime_error("Failed to map shared memory: " + std::string(strerror(errno)));
        }
        
        // Zero out the memory
        std::memset(data_, 0, size_);
        
        std::cout << "Shared memory created successfully:" << std::endl;
        std::cout << "  fd: " << fd_ << std::endl;
        std::cout << "  address: " << data_ << std::endl;
        std::cout << "=== SharedMemory creation complete ===\n" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error creating shared memory: " << e.what() << std::endl;
        throw;
    }
}

SharedMemory::~SharedMemory() {
    std::cout << "\n=== Destroying SharedMemory ===" << std::endl;
    std::cout << "Name: " << name_ << std::endl;
    
    try {
        // Unmap memory
        if (data_ != nullptr && data_ != MAP_FAILED) {
            if (munmap(data_, size_) == -1) {
                std::cerr << "Failed to unmap shared memory: " << strerror(errno) << std::endl;
            }
            data_ = nullptr;
        }
        
        // Close file descriptor
        if (fd_ != -1) {
            if (close(fd_) == -1) {
                std::cerr << "Failed to close shared memory fd: " << strerror(errno) << std::endl;
            }
            fd_ = -1;
        }
        
        // Remove shared memory object
        if (shm_unlink(name_.c_str()) == -1) {
            std::cerr << "Failed to unlink shared memory: " << strerror(errno) << std::endl;
        }
        
        std::cout << "Shared memory destroyed successfully" << std::endl;
        std::cout << "=== SharedMemory destruction complete ===\n" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error destroying shared memory: " << e.what() << std::endl;
    }
}

void SharedMemory::remove(const std::string& name) {
    std::cout << "Removing shared memory: " << name << std::endl;
    if (shm_unlink(name.c_str()) == -1 && errno != ENOENT) {
        std::cerr << "Failed to remove shared memory: " << std::strerror(errno) << std::endl;
    }
}

} // namespace lockfree 