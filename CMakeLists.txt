cmake_minimum_required(VERSION 3.15)
project(LockfreeMessaging VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Set Boost policy
cmake_policy(SET CMP0167 NEW)

# Find required packages
find_package(Boost 1.88.0 REQUIRED)
find_package(GTest REQUIRED)

# Add compiler flags for optimization and warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic -O3 -march=native)
endif()

# Add source files
add_library(lockfree_messaging
    backend/src/shared_memory.hpp
    backend/src/shared_memory.cpp
    backend/src/message_bus.hpp
    backend/src/message_bus.cpp
    backend/src/ring_buffer.hpp
)

# Link dependencies and include directories
target_link_libraries(lockfree_messaging
    PUBLIC
    Boost::boost
)

# Add include directories
target_include_directories(lockfree_messaging
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/backend/src
    ${Boost_INCLUDE_DIRS}
)

# Add tests
enable_testing()
add_executable(tests backend/src/tests.cpp)
target_link_libraries(tests PRIVATE lockfree_messaging GTest::GTest GTest::Main)
target_include_directories(tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/backend/src ${Boost_INCLUDE_DIRS})
add_test(NAME tests COMMAND tests)