cmake_minimum_required(VERSION 3.10)
project(MPMCPacketQueue)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add optimization flags
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -march=native --std=c++20")

# Find Google Test
find_package(GTest REQUIRED)

# Create test executable
add_executable(mpmc_queue_tests
    mpmc_packet_queue_test.cpp
)

target_link_libraries(mpmc_queue_tests
    GTest::gtest
    GTest::gtest_main
    pthread
)

# Enable testing
enable_testing()
add_test(NAME MPMCQueueTests COMMAND mpmc_queue_tests)
