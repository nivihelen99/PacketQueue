// g++ mpmc_packet_queue_test.cpp -lgtest -lgtest_main -std=c++20  -lpthread


#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <random>
#include <algorithm>
#include <set>
#include "mpmc_packet_queue.h" // Include the header file

class MPMC_PacketQueueTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Common setup for all tests
    }

    void TearDown() override {
        // Common cleanup for all tests
    }

    // Helper function to create test packets
    std::vector<Packet> create_test_packets(size_t count, size_t start_id = 0) {
        std::vector<Packet> packets;
        packets.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            packets.emplace_back(start_id + i);
        }
        return packets;
    }

    // Helper to create packet with data
    Packet create_packet_with_data(size_t id, const std::string& data) {
        static std::vector<std::vector<uint8_t>> data_storage;
        data_storage.emplace_back(data.begin(), data.end());
        return Packet(data_storage.back().data(), data_storage.back().size(), 
                     PacketPriority::Medium, id);
    }
};

// Basic functionality tests
TEST_F(MPMC_PacketQueueTest, ConstructorValidation) {
    // Valid construction
    EXPECT_NO_THROW(MPMC_PacketQueue queue(8));
    EXPECT_NO_THROW(MPMC_PacketQueue queue(1024));
    
    // Invalid construction
    EXPECT_THROW(MPMC_PacketQueue queue(0), std::invalid_argument);
}

TEST_F(MPMC_PacketQueueTest, BasicEnqueueDequeue) {
    MPMC_PacketQueue queue(8);
    
    // Test empty queue
    EXPECT_TRUE(queue.empty());
    EXPECT_EQ(queue.size(), 0);
    EXPECT_FALSE(queue.full());
    
    // Enqueue a packet
    Packet packet(42);
    EXPECT_TRUE(queue.enqueue(packet));
    EXPECT_FALSE(queue.empty());
    EXPECT_EQ(queue.size(), 1);
    
    // Dequeue the packet
    auto dequeued = queue.dequeue();
    EXPECT_TRUE(dequeued.has_value());
    EXPECT_EQ(dequeued->id, 42);
    EXPECT_TRUE(queue.empty());
    EXPECT_EQ(queue.size(), 0);
}

TEST_F(MPMC_PacketQueueTest, MoveSemantics) {
    MPMC_PacketQueue queue(8);
    
    // Test move enqueue
    Packet packet(123);
    packet.priority = PacketPriority::High;
    
    EXPECT_TRUE(queue.enqueue(std::move(packet)));
    
    auto dequeued = queue.dequeue();
    EXPECT_TRUE(dequeued.has_value());
    EXPECT_EQ(dequeued->id, 123);
    EXPECT_EQ(dequeued->priority, PacketPriority::High);
}

TEST_F(MPMC_PacketQueueTest, QueueCapacity) {
    constexpr size_t capacity = 4;
    MPMC_PacketQueue queue(capacity);
    
    EXPECT_EQ(queue.capacity(), 4); // Should round up to power of 2
    
    // Fill the queue
    for (size_t i = 0; i < capacity; ++i) {
        EXPECT_TRUE(queue.enqueue(Packet(i)));
    }
    
    EXPECT_TRUE(queue.full());
    EXPECT_EQ(queue.size(), capacity);
    
    // Should fail to enqueue when full
    EXPECT_FALSE(queue.enqueue(Packet(999)));
    
    // Dequeue one and try again
    auto packet = queue.dequeue();
    EXPECT_TRUE(packet.has_value());
    EXPECT_FALSE(queue.full());
    EXPECT_TRUE(queue.enqueue(Packet(999)));
}

TEST_F(MPMC_PacketQueueTest, EmptyQueueDequeue) {
    MPMC_PacketQueue queue(8);
    
    // Dequeue from empty queue should return nullopt
    auto result = queue.dequeue();
    EXPECT_FALSE(result.has_value());
}

TEST_F(MPMC_PacketQueueTest, BatchOperations) {
    MPMC_PacketQueue queue(16);
    
    // Test batch enqueue
    auto packets = create_test_packets(8);
    size_t enqueued = queue.enqueue_batch(my_std::span<const Packet>(packets));
    EXPECT_EQ(enqueued, 8);
    EXPECT_EQ(queue.size(), 8);
    
    // Test batch dequeue
    std::vector<Packet> dequeued_packets(8);
    size_t dequeued = queue.dequeue_batch(my_std::span<Packet>(dequeued_packets));
    EXPECT_EQ(dequeued, 8);
    EXPECT_TRUE(queue.empty());
    
    // Verify packet IDs
    for (size_t i = 0; i < 8; ++i) {
        EXPECT_EQ(dequeued_packets[i].id, i);
    }
}

TEST_F(MPMC_PacketQueueTest, PartialBatchOperations) {
    MPMC_PacketQueue queue(4);
    
    // Try to enqueue more than capacity
    auto packets = create_test_packets(8);
    size_t enqueued = queue.enqueue_batch(my_std::span<const Packet>(packets));
    EXPECT_EQ(enqueued, 4); // Should only enqueue up to capacity
    EXPECT_TRUE(queue.full());
    
    // Try to dequeue more than available
    std::vector<Packet> dequeued_packets(8);
    size_t dequeued = queue.dequeue_batch(my_std::span<Packet>(dequeued_packets));
    EXPECT_EQ(dequeued, 4); // Should only dequeue what's available
    EXPECT_TRUE(queue.empty());
}

TEST_F(MPMC_PacketQueueTest, TryOperations) {
    MPMC_PacketQueue queue(2);
    
    // Try enqueue on empty queue
    EXPECT_TRUE(queue.try_enqueue(Packet(1)));
    EXPECT_TRUE(queue.try_enqueue(Packet(2)));
    
    // Try enqueue on full queue
    EXPECT_FALSE(queue.try_enqueue(Packet(3)));
    
    // Try dequeue
    auto packet1 = queue.try_dequeue();
    EXPECT_TRUE(packet1.has_value());
    EXPECT_EQ(packet1->id, 1);
    
    auto packet2 = queue.try_dequeue();
    EXPECT_TRUE(packet2.has_value());
    EXPECT_EQ(packet2->id, 2);
    
    // Try dequeue on empty queue
    auto packet3 = queue.try_dequeue();
    EXPECT_FALSE(packet3.has_value());
}

TEST_F(MPMC_PacketQueueTest, PacketPriority) {
    Packet low_priority(1);
    low_priority.priority = PacketPriority::Low;
    
    Packet high_priority(2);
    high_priority.priority = PacketPriority::High;
    
    // Test comparison
    EXPECT_TRUE(low_priority < high_priority);
    EXPECT_FALSE(high_priority < low_priority);
    
    // Test equality
    Packet same_packet(2);
    same_packet.priority = PacketPriority::High;
    EXPECT_TRUE(high_priority == same_packet);
    EXPECT_FALSE(high_priority != same_packet);
}

TEST_F(MPMC_PacketQueueTest, PacketValidation) {
    Packet invalid_packet;
    EXPECT_FALSE(invalid_packet.is_valid());
    
    std::string test_data = "test data";
    Packet valid_packet(reinterpret_cast<uint8_t*>(test_data.data()), 
                       test_data.size(), PacketPriority::Medium, 1);
    EXPECT_TRUE(valid_packet.is_valid());
    
    valid_packet.reset();
    EXPECT_FALSE(valid_packet.is_valid());
}

// Multi-threading tests
TEST_F(MPMC_PacketQueueTest, SingleProducerSingleConsumer) {
    constexpr size_t num_packets = 10000;
    MPMC_PacketQueue queue(1024);
    std::atomic<bool> done{false};
    std::vector<size_t> received_ids;
    received_ids.reserve(num_packets);
    
    // Consumer thread
    std::thread consumer([&]() {
        while (!done.load() || !queue.empty()) {
            auto packet = queue.dequeue();
            if (packet.has_value()) {
                received_ids.push_back(packet->id);
            } else {
                std::this_thread::yield();
            }
        }
    });
    
    // Producer thread
    std::thread producer([&]() {
        for (size_t i = 0; i < num_packets; ++i) {
            while (!queue.enqueue(Packet(i))) {
                std::this_thread::yield();
            }
        }
        done.store(true);
    });
    
    producer.join();
    consumer.join();
    
    // Verify all packets were received
    EXPECT_EQ(received_ids.size(), num_packets);
    std::sort(received_ids.begin(), received_ids.end());
    for (size_t i = 0; i < num_packets; ++i) {
        EXPECT_EQ(received_ids[i], i);
    }
}

TEST_F(MPMC_PacketQueueTest, MultipleProducersMultipleConsumers) {
    constexpr size_t num_producers = 4;
    constexpr size_t num_consumers = 4;
    constexpr size_t packets_per_producer = 1000;
    constexpr size_t total_packets = num_producers * packets_per_producer;
    
    MPMC_PacketQueue queue(512, true); // Enable statistics
    std::atomic<size_t> producers_done{0};
    std::atomic<size_t> total_consumed{0};
    std::vector<std::set<size_t>> consumed_per_thread(num_consumers);
    
    // Consumer threads
    std::vector<std::thread> consumers;
    consumers.reserve(num_consumers);
    
    for (size_t i = 0; i < num_consumers; ++i) {
        consumers.emplace_back([&, i]() {
            while (producers_done.load() < num_producers || !queue.empty()) {
                auto packet = queue.dequeue();
                if (packet.has_value()) {
                    consumed_per_thread[i].insert(packet->id);
                    total_consumed.fetch_add(1);
                } else {
                    std::this_thread::yield();
                }
            }
        });
    }
    
    // Producer threads
    std::vector<std::thread> producers;
    producers.reserve(num_producers);
    
    for (size_t i = 0; i < num_producers; ++i) {
        producers.emplace_back([&, i]() {
            size_t start_id = i * packets_per_producer;
            for (size_t j = 0; j < packets_per_producer; ++j) {
                while (!queue.enqueue(Packet(start_id + j))) {
                    std::this_thread::yield();
                }
            }
            producers_done.fetch_add(1);
        });
    }
    
    // Wait for all threads to complete
    for (auto& producer : producers) {
        producer.join();
    }
    for (auto& consumer : consumers) {
        consumer.join();
    }
    
    // Verify results
    EXPECT_EQ(total_consumed.load(), total_packets);
    EXPECT_TRUE(queue.empty());
    
    // Verify all packets were consumed exactly once
    std::set<size_t> all_consumed;
    for (const auto& consumed_set : consumed_per_thread) {
        for (size_t id : consumed_set) {
            EXPECT_TRUE(all_consumed.insert(id).second) << "Packet " << id << " consumed multiple times";
        }
    }
    EXPECT_EQ(all_consumed.size(), total_packets);
    
    // Check statistics
    const auto& stats = queue.get_stats();
    EXPECT_GT(stats.enqueue_successes.load(), 0);
    EXPECT_GT(stats.dequeue_successes.load(), 0);
    EXPECT_GE(stats.get_enqueue_success_rate(), 0.0);
    EXPECT_LE(stats.get_enqueue_success_rate(), 1.0);
}

TEST_F(MPMC_PacketQueueTest, HighContentionStressTest) {
    constexpr size_t num_threads = 8;
    constexpr size_t operations_per_thread = 5000;
    constexpr size_t queue_capacity = 64;
    
    MPMC_PacketQueue queue(queue_capacity, true);
    std::atomic<size_t> total_enqueued{0};
    std::atomic<size_t> total_dequeued{0};
    std::atomic<bool> should_stop{false};
    
    std::vector<std::thread> threads;
    threads.reserve(num_threads);
    
    // Mix of producer and consumer threads
    for (size_t i = 0; i < num_threads; ++i) {
        if (i % 2 == 0) {
            // Producer thread
            threads.emplace_back([&, i]() {
                size_t enqueued = 0;
                size_t base_id = i * operations_per_thread;
                
                while (enqueued < operations_per_thread && !should_stop.load()) {
                    if (queue.enqueue(Packet(base_id + enqueued))) {
                        ++enqueued;
                    } else {
                        std::this_thread::yield();
                    }
                }
                total_enqueued.fetch_add(enqueued);
            });
        } else {
            // Consumer thread
            threads.emplace_back([&]() {
                size_t dequeued = 0;
                
                while (dequeued < operations_per_thread && !should_stop.load()) {
                    auto packet = queue.dequeue();
                    if (packet.has_value()) {
                        ++dequeued;
                    } else {
                        std::this_thread::yield();
                    }
                }
                total_dequeued.fetch_add(dequeued);
            });
        }
    }
    
    // Set timeout to prevent infinite loops
    std::thread timeout_thread([&]() {
        std::this_thread::sleep_for(std::chrono::seconds(30));
        should_stop.store(true);
    });
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    should_stop.store(true);
    timeout_thread.join();
    
    // Drain remaining packets
    while (!queue.empty()) {
        auto packet = queue.dequeue();
        if (packet.has_value()) {
            total_dequeued.fetch_add(1);
        }
    }
    
    EXPECT_EQ(total_enqueued.load(), total_dequeued.load());
    EXPECT_GT(queue.get_stats().contention_events.load(), 0);
}

TEST_F(MPMC_PacketQueueTest, BatchOperationsMultiThreaded) {
    constexpr size_t num_threads = 4;
    constexpr size_t batches_per_thread = 100;
    constexpr size_t batch_size = 10;
    constexpr size_t total_packets = num_threads * batches_per_thread * batch_size;
    
    MPMC_PacketQueue queue(512);
    std::atomic<size_t> packets_produced{0};
    std::atomic<size_t> packets_consumed{0};
    std::atomic<bool> production_done{false};
    
    // Producer threads using batch enqueue
    std::vector<std::thread> producers;
    for (size_t t = 0; t < num_threads / 2; ++t) {
        producers.emplace_back([&, t]() {
            for (size_t b = 0; b < batches_per_thread; ++b) {
                auto packets = create_test_packets(batch_size, 
                    t * batches_per_thread * batch_size + b * batch_size);
                
                size_t enqueued = 0;
                while (enqueued < batch_size) {
                    my_std::span<const Packet> remaining(packets.begin() + enqueued, 
                                                     packets.end());
                    enqueued += queue.enqueue_batch(remaining);
                    if (enqueued < batch_size) {
                        std::this_thread::yield();
                    }
                }
                packets_produced.fetch_add(batch_size);
            }
        });
    }
    
    // Consumer threads using batch dequeue
    std::vector<std::thread> consumers;
    for (size_t t = 0; t < num_threads / 2; ++t) {
        consumers.emplace_back([&]() {
            while (!production_done.load() || !queue.empty()) {
                std::vector<Packet> batch(batch_size);
                size_t dequeued = queue.dequeue_batch(my_std::span<Packet>(batch));
                packets_consumed.fetch_add(dequeued);
                if (dequeued == 0) {
                    std::this_thread::yield();
                }
            }
        });
    }
    
    // Wait for producers
    for (auto& producer : producers) {
        producer.join();
    }
    production_done.store(true);
    
    // Wait for consumers
    for (auto& consumer : consumers) {
        consumer.join();
    }
    
    EXPECT_EQ(packets_produced.load(), total_packets / 2); // Only half threads are producers
    EXPECT_EQ(packets_consumed.load(), packets_produced.load());
}

TEST_F(MPMC_PacketQueueTest, MemoryOrderingTest) {
    // Test for proper memory ordering under high contention
    constexpr size_t iterations = 1000;
    // constexpr size_t num_threads = std::thread::hardware_concurrency(); // anand
    size_t num_threads = std::thread::hardware_concurrency();
    
    for (size_t iter = 0; iter < iterations; ++iter) {
        MPMC_PacketQueue queue(16);
        std::atomic<bool> start{false};
        std::atomic<size_t> enqueue_count{0};
        std::atomic<size_t> dequeue_count{0};
        
        std::vector<std::thread> threads;
        
        // Create equal number of producer and consumer threads
        for (size_t i = 0; i < num_threads; ++i) {
            if (i % 2 == 0) {
                threads.emplace_back([&, i]() {
                    while (!start.load()) { std::this_thread::yield(); }
                    
                    if (queue.enqueue(Packet(i))) {
                        enqueue_count.fetch_add(1);
                    }
                });
            } else {
                threads.emplace_back([&]() {
                    while (!start.load()) { std::this_thread::yield(); }
                    
                    auto packet = queue.dequeue();
                    if (packet.has_value()) {
                        dequeue_count.fetch_add(1);
                    }
                });
            }
        }
        
        // Start all threads simultaneously
        start.store(true);
        
        for (auto& thread : threads) {
            thread.join();
        }
        
        // Drain remaining packets
        while (!queue.empty()) {
            auto packet = queue.dequeue();
            if (packet.has_value()) {
                dequeue_count.fetch_add(1);
            }
        }
        
        EXPECT_EQ(enqueue_count.load(), dequeue_count.load());
    }
}

// Performance benchmarks
TEST_F(MPMC_PacketQueueTest, SingleThreadedPerformanceBenchmark) {
    constexpr size_t num_operations = 1000000;
    MPMC_PacketQueue queue(1024);
    
    auto start = std::chrono::high_resolution_clock::now();
    
    // Alternating enqueue/dequeue
    for (size_t i = 0; i < num_operations; ++i) {
        EXPECT_TRUE(queue.enqueue(Packet(i)));
        auto packet = queue.dequeue();
        EXPECT_TRUE(packet.has_value());
        EXPECT_EQ(packet->id, i);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
    
    double ops_per_second = (2.0 * num_operations * 1e9) / duration.count();
    
    // This is more of a benchmark than a test - adjust expectations based on hardware
    EXPECT_GT(ops_per_second, 1000000); // At least 1M ops/sec
    
    std::cout << "Single-threaded performance: " << ops_per_second 
              << " operations/second" << std::endl;
}

TEST_F(MPMC_PacketQueueTest, BatchPerformanceBenchmark) {
    constexpr size_t num_batches = 10000;
    constexpr size_t batch_size = 100;
    MPMC_PacketQueue queue(2048);
    
    auto packets = create_test_packets(batch_size);
    std::vector<Packet> dequeued_packets(batch_size);
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (size_t i = 0; i < num_batches; ++i) {
        EXPECT_EQ(queue.enqueue_batch(my_std::span<const Packet>(packets)), batch_size);
        EXPECT_EQ(queue.dequeue_batch(my_std::span<Packet>(dequeued_packets)), batch_size);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
    
    double packets_per_second = (2.0 * num_batches * batch_size * 1e9) / duration.count();
    
    EXPECT_GT(packets_per_second, 10000000); // At least 10M packets/sec
    
    std::cout << "Batch performance: " << packets_per_second 
              << " packets/second" << std::endl;
}

// Edge cases and error conditions
TEST_F(MPMC_PacketQueueTest, PowerOfTwoCapacity) {
    // Test that capacity is properly rounded up to power of 2
    MPMC_PacketQueue queue1(3);
    EXPECT_EQ(queue1.capacity(), 4);
    
    MPMC_PacketQueue queue2(5);
    EXPECT_EQ(queue2.capacity(), 8);
    
    MPMC_PacketQueue queue3(16);
    EXPECT_EQ(queue3.capacity(), 16);
    
    MPMC_PacketQueue queue4(17);
    EXPECT_EQ(queue4.capacity(), 32);
}

TEST_F(MPMC_PacketQueueTest, StatisticsTest) {
    MPMC_PacketQueue queue(8, true); // Enable statistics
    
    // Initial state
    const auto& stats = queue.get_stats();
    EXPECT_EQ(stats.enqueue_attempts.load(), 0);
    EXPECT_EQ(stats.enqueue_successes.load(), 0);
    
    // Perform operations
    EXPECT_TRUE(queue.enqueue(Packet(1)));
    EXPECT_TRUE(queue.enqueue(Packet(2)));
    
    auto packet = queue.dequeue();
    EXPECT_TRUE(packet.has_value());
    
    // Check statistics
    EXPECT_GE(stats.enqueue_attempts.load(), 2);
    EXPECT_GE(stats.enqueue_successes.load(), 2);
    EXPECT_GE(stats.dequeue_attempts.load(), 1);
    EXPECT_GE(stats.dequeue_successes.load(), 1);
    
    // Test reset
    queue.reset_stats();
    EXPECT_EQ(stats.enqueue_attempts.load(), 0);
    EXPECT_EQ(stats.enqueue_successes.load(), 0);
}

TEST_F(MPMC_PacketQueueTest, MemoryUsageTest) {
    MPMC_PacketQueue queue(64);
    size_t memory_usage = queue.memory_usage();
    
    // Should be reasonable (not exact due to alignment and padding)
    EXPECT_GT(memory_usage, sizeof(MPMC_PacketQueue));
    EXPECT_LT(memory_usage, 1024 * 1024); // Less than 1MB for this small queue
}

TEST_F(MPMC_PacketQueueTest, LargeCapacityTest) {
    // Test with large capacity (but not too large to avoid memory issues in CI)
    constexpr size_t large_capacity = 65536;
    MPMC_PacketQueue queue(large_capacity);
    
    EXPECT_EQ(queue.capacity(), large_capacity);
    EXPECT_TRUE(queue.empty());
    
    // Fill partially and test
    constexpr size_t test_count = 1000;
    for (size_t i = 0; i < test_count; ++i) {
        EXPECT_TRUE(queue.enqueue(Packet(i)));
    }
    
    EXPECT_EQ(queue.size(), test_count);
    
    for (size_t i = 0; i < test_count; ++i) {
        auto packet = queue.dequeue();
        EXPECT_TRUE(packet.has_value());
        EXPECT_EQ(packet->id, i);
    }
    
    EXPECT_TRUE(queue.empty());
}

// Test main function
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

