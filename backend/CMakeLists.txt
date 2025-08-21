cmake_minimum_required(VERSION 3.10)
project(backend)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(Threads REQUIRED)
find_package(Boost REQUIRED COMPONENTS system)

# Add nlohmann_json
include(FetchContent)
FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.11.2/json.tar.xz
)
FetchContent_MakeAvailable(json)

# Add source files
set(SOURCES
    src/server.cpp
    src/message_bus.cpp
    src/shared_memory.cpp
    src/market_data/replay_engine.cpp
)

# Add header files
set(HEADERS
    src/server.hpp
    src/message_bus.hpp
    src/shared_memory.hpp
    src/ring_buffer.hpp
    src/market_data/finnhub_client.hpp
    src/market_data/replay_engine.hpp
    src/market_data/market_data_types.hpp
)

# Create executable
add_executable(backend ${SOURCES} ${HEADERS})

# Link libraries
target_link_libraries(backend
    PRIVATE
    Threads::Threads
    Boost::system
    nlohmann_json::nlohmann_json
    pthread
)

# Include directories
target_include_directories(backend
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/market_data
) 