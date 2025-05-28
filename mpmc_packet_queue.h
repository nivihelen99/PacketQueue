#pragma once

#include <atomic>
#include <vector>
#include <optional>
#include <span>
#include <thread>
#include <cstdint>
#include <stdexcept>
#include <memory>
#include <type_traits>

#include "my_span.h"

// Cache line size for most modern processors
constexpr size_t CACHE_LINE_SIZE = 64;

// Define Packet structure with improved design
enum class PacketPriority : uint8_t { 
    Low = 0, 
    Medium = 1, 
    High = 2, 
    Control = 3 
};

struct Packet {
    uint8_t* data = nullptr;
    size_t length = 0;
    PacketPriority priority = PacketPriority::Low;
    size_t id = 0;

    // Default constructor
    Packet() = default;
    
    // Constructor for testing
    explicit Packet(size_t i) : id(i) {}
    
    // Full constructor
    Packet(uint8_t* d, size_t l, PacketPriority p, size_t i = 0)
        : data(d), length(l), priority(p), id(i) {}

    // Move constructor and assignment
    Packet(Packet&& other) noexcept 
        : data(other.data), length(other.length), 
          priority(other.priority), id(other.id) {
        other.data = nullptr;
        other.length = 0;
        other.id = 0;
    }

    Packet& operator=(Packet&& other) noexcept {
        if (this != &other) {
            data = other.data;
            length = other.length;
            priority = other.priority;
            id = other.id;
            other.data = nullptr;
            other.length = 0;
            other.id = 0;
        }
        return *this;
    }

    // Copy constructor and assignment
    Packet(const Packet& other) = default;
    Packet& operator=(const Packet& other) = default;

    // Comparison operators
    bool operator<(const Packet& other) const noexcept {
        if (priority != other.priority) {
            return static_cast<uint8_t>(priority) < static_cast<uint8_t>(other.priority);
        }
        return id < other.id;
    }

    bool operator==(const Packet& other) const noexcept {
        return id == other.id && priority == other.priority;
    }

    bool operator!=(const Packet& other) const noexcept {
        return !(*this == other);
    }

    // Utility functions
    bool is_valid() const noexcept {
        return data != nullptr && length > 0;
    }

    void reset() noexcept {
        data = nullptr;
        length = 0;
        priority = PacketPriority::Low;
        id = 0;
    }
};

// Statistics for monitoring queue performance
struct QueueStats {
    std::atomic<uint64_t> enqueue_attempts{0};
    std::atomic<uint64_t> enqueue_successes{0};
    std::atomic<uint64_t> dequeue_attempts{0};
    std::atomic<uint64_t> dequeue_successes{0};
    std::atomic<uint64_t> batch_enqueues{0};
    std::atomic<uint64_t> batch_dequeues{0};
    std::atomic<uint64_t> contention_events{0};

    void reset() noexcept {
        enqueue_attempts.store(0, std::memory_order_relaxed);
        enqueue_successes.store(0, std::memory_order_relaxed);
        dequeue_attempts.store(0, std::memory_order_relaxed);
        dequeue_successes.store(0, std::memory_order_relaxed);
        batch_enqueues.store(0, std::memory_order_relaxed);
        batch_dequeues.store(0, std::memory_order_relaxed);
        contention_events.store(0, std::memory_order_relaxed);
    }

    double get_enqueue_success_rate() const noexcept {
        auto attempts = enqueue_attempts.load(std::memory_order_relaxed);
        if (attempts == 0) return 0.0;
        return static_cast<double>(enqueue_successes.load(std::memory_order_relaxed)) / attempts;
    }

    double get_dequeue_success_rate() const noexcept {
        auto attempts = dequeue_attempts.load(std::memory_order_relaxed);
        if (attempts == 0) return 0.0;
        return static_cast<double>(dequeue_successes.load(std::memory_order_relaxed)) / attempts;
    }
};

class MPMC_PacketQueue {
private:
    struct alignas(CACHE_LINE_SIZE) Slot {
        Packet packet;
        std::atomic<size_t> seq;
        
        Slot() : packet(), seq(0) {}
        
        static_assert(sizeof(Packet) + sizeof(std::atomic<size_t>) <= CACHE_LINE_SIZE, 
                      "The combined size of Packet and std::atomic<size_t> must not exceed CACHE_LINE_SIZE.");
        // Prevent false sharing by padding
        char padding[CACHE_LINE_SIZE - sizeof(Packet) - sizeof(std::atomic<size_t>)];
    };

    // Utility function to round up to power of two
    static constexpr size_t round_up_to_power_of_two(size_t v) noexcept {
        if (v <= 1) return 2; // Minimum capacity 2
        if (v > (SIZE_MAX >> 1)) return SIZE_MAX; // Prevent overflow
        
        // Use builtin if available (GCC/Clang)
        #if defined(__GNUC__) || defined(__clang__)
        return size_t(1) << (64 - __builtin_clzll(v - 1));
        #else
        // Fallback implementation
        v--;
        v |= v >> 1; v |= v >> 2; v |= v >> 4; v |= v >> 8; v |= v >> 16;
        #if SIZE_MAX > 0xFFFFFFFF
        v |= v >> 32;
        #endif
        return ++v;
        #endif
    }

    // Backoff strategy for contention
    class Backoff {
        static constexpr int MAX_SPINS = 16;
        static constexpr int MAX_YIELDS = 64;
        int count_ = 0;

    public:
        void operator()() noexcept {
            if (count_ < MAX_SPINS) {
                // Spin with CPU pause
                for (int i = 0; i < (1 << count_); ++i) {
                    #if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
                    __builtin_ia32_pause();
                    #elif defined(__aarch64__) || defined(_M_ARM64)
                    __asm__ __volatile__("yield" ::: "memory");
                    #endif
                }
                ++count_;
            } else if (count_ < MAX_SPINS + MAX_YIELDS) {
                std::this_thread::yield();
                ++count_;
            } else {
                std::this_thread::sleep_for(std::chrono::microseconds(1));
            }
        }

        void reset() noexcept { count_ = 0; }
    };

    const size_t capacity_;
    const size_t mask_;
    
    // Align to cache line boundaries to prevent false sharing
    alignas(CACHE_LINE_SIZE) std::unique_ptr<Slot[]> buffer_;
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> head_seq_;
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> tail_seq_;
    
    // Statistics (optional, can be disabled for performance)
    mutable QueueStats stats_;
    const bool enable_stats_;

public:
    explicit MPMC_PacketQueue(size_t capacity, bool enable_stats = false)
        : capacity_(round_up_to_power_of_two(capacity)),
          mask_(capacity_ - 1),
          buffer_(std::make_unique<Slot[]>(capacity_)),
          head_seq_(0),
          tail_seq_(0),
          enable_stats_(enable_stats) {
        
        if (capacity == 0) {
            throw std::invalid_argument("Capacity must be greater than 0");
        }
        
        if (capacity_ > (SIZE_MAX >> 1)) {
            throw std::invalid_argument("Capacity too large");
        }

        // Initialize sequence numbers
        for (size_t i = 0; i < capacity_; ++i) {
            buffer_[i].seq.store(i, std::memory_order_relaxed);
        }
    }

    // Deleted copy/move operations due to atomics and const members
    MPMC_PacketQueue(const MPMC_PacketQueue&) = delete;
    MPMC_PacketQueue& operator=(const MPMC_PacketQueue&) = delete;
    MPMC_PacketQueue(MPMC_PacketQueue&&) = delete;
    MPMC_PacketQueue& operator=(MPMC_PacketQueue&&) = delete;

    ~MPMC_PacketQueue() = default;

    // Single packet enqueue with improved performance
    bool enqueue(const Packet& packet) noexcept {
        if (enable_stats_) {
            stats_.enqueue_attempts.fetch_add(1, std::memory_order_relaxed);
        }

        Backoff backoff;
        size_t tail = tail_seq_.load(std::memory_order_relaxed);

        while (true) {
            Slot& slot = buffer_[tail & mask_];
            size_t seq = slot.seq.load(std::memory_order_acquire);
            intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(tail);

            if (diff == 0) {
                // Slot is ready, try to claim it
                if (tail_seq_.compare_exchange_weak(tail, tail + 1,
                                                    std::memory_order_relaxed,
                                                    std::memory_order_relaxed)) {
                    slot.packet = packet;
                    slot.seq.store(tail + 1, std::memory_order_release);
                    
                    if (enable_stats_) {
                        stats_.enqueue_successes.fetch_add(1, std::memory_order_relaxed);
                    }
                    return true;
                }
                backoff.reset();
            } else if (diff < 0) {
                // Queue might be full, check explicitly
                size_t head = head_seq_.load(std::memory_order_acquire);
                if (tail - head >= capacity_) {
                    return false; // Queue is definitively full
                }
                
                if (enable_stats_) {
                    stats_.contention_events.fetch_add(1, std::memory_order_relaxed);
                }
                backoff();
                tail = tail_seq_.load(std::memory_order_relaxed);
            } else {
                // Unexpected state, reload and retry
                backoff();
                tail = tail_seq_.load(std::memory_order_relaxed);
            }
        }
    }

    // Move version for better performance
    bool enqueue(Packet&& packet) noexcept {
        if (enable_stats_) {
            stats_.enqueue_attempts.fetch_add(1, std::memory_order_relaxed);
        }

        Backoff backoff;
        size_t tail = tail_seq_.load(std::memory_order_relaxed);

        while (true) {
            Slot& slot = buffer_[tail & mask_];
            size_t seq = slot.seq.load(std::memory_order_acquire);
            intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(tail);

            if (diff == 0) {
                if (tail_seq_.compare_exchange_weak(tail, tail + 1,
                                                    std::memory_order_relaxed,
                                                    std::memory_order_relaxed)) {
                    slot.packet = std::move(packet);
                    slot.seq.store(tail + 1, std::memory_order_release);
                    
                    if (enable_stats_) {
                        stats_.enqueue_successes.fetch_add(1, std::memory_order_relaxed);
                    }
                    return true;
                }
                backoff.reset();
            } else if (diff < 0) {
                size_t head = head_seq_.load(std::memory_order_acquire);
                if (tail - head >= capacity_) {
                    return false;
                }
                
                if (enable_stats_) {
                    stats_.contention_events.fetch_add(1, std::memory_order_relaxed);
                }
                backoff();
                tail = tail_seq_.load(std::memory_order_relaxed);
            } else {
                backoff();
                tail = tail_seq_.load(std::memory_order_relaxed);
            }
        }
    }

    // Single packet dequeue with improved performance
    std::optional<Packet> dequeue() noexcept {
        if (enable_stats_) {
            stats_.dequeue_attempts.fetch_add(1, std::memory_order_relaxed);
        }

        Backoff backoff;
        size_t head = head_seq_.load(std::memory_order_relaxed);

        while (true) {
            Slot& slot = buffer_[head & mask_];
            size_t seq = slot.seq.load(std::memory_order_acquire);
            intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(head + 1);

            if (diff == 0) {
                // Slot has data, try to claim it
                if (head_seq_.compare_exchange_weak(head, head + 1,
                                                    std::memory_order_relaxed,
                                                    std::memory_order_relaxed)) {
                    Packet packet = std::move(slot.packet);
                    slot.seq.store(head + capacity_, std::memory_order_release);
                    
                    if (enable_stats_) {
                        stats_.dequeue_successes.fetch_add(1, std::memory_order_relaxed);
                    }
                    return packet;
                }
                backoff.reset();
            } else if (diff < 0) {
                // Queue might be empty, check explicitly
                size_t tail = tail_seq_.load(std::memory_order_acquire);
                if (head >= tail) {
                    return std::nullopt; // Queue is definitively empty
                }
                
                if (enable_stats_) {
                    stats_.contention_events.fetch_add(1, std::memory_order_relaxed);
                }
                backoff();
                head = head_seq_.load(std::memory_order_relaxed);
            } else {
                // Unexpected state, reload and retry
                backoff();
                head = head_seq_.load(std::memory_order_relaxed);
            }
        }
    }

    // Improved batch enqueue
    size_t enqueue_batch(my_std::span<const Packet> packets) noexcept {
        if (packets.empty()) return 0;
        
        if (enable_stats_) {
            stats_.batch_enqueues.fetch_add(1, std::memory_order_relaxed);
        }

        size_t enqueued_count = 0;
        Backoff backoff;

        while (enqueued_count < packets.size()) {
            size_t tail = tail_seq_.load(std::memory_order_acquire);
            size_t head = head_seq_.load(std::memory_order_acquire);
            
            if (tail - head >= capacity_) {
                break; // Queue is full
            }

            size_t available_space = capacity_ - (tail - head);
            size_t batch_size = std::min(packets.size() - enqueued_count, available_space);
            
            if (batch_size == 0) {
                backoff();
                continue;
            }

            if (tail_seq_.compare_exchange_weak(tail, tail + batch_size,
                                               std::memory_order_acq_rel,
                                               std::memory_order_acquire)) {
                // Successfully reserved slots
                for (size_t i = 0; i < batch_size; ++i) {
                    Slot& slot = buffer_[(tail + i) & mask_];
                    
                    // Wait for slot to be ready
                    while (slot.seq.load(std::memory_order_acquire) != tail + i) {
                        std::this_thread::yield();
                    }
                    
                    slot.packet = packets[enqueued_count + i];
                    slot.seq.store(tail + i + 1, std::memory_order_release);
                }
                enqueued_count += batch_size;
                backoff.reset();
            } else {
                backoff();
            }
        }
        return enqueued_count;
    }

    // Improved batch dequeue
    size_t dequeue_batch(my_std::span<Packet> packets) noexcept {
        if (packets.empty()) return 0;
        
        if (enable_stats_) {
            stats_.batch_dequeues.fetch_add(1, std::memory_order_relaxed);
        }

        size_t dequeued_count = 0;
        Backoff backoff;

        while (dequeued_count < packets.size()) {
            size_t head = head_seq_.load(std::memory_order_acquire);
            size_t tail = tail_seq_.load(std::memory_order_acquire);
            
            if (head >= tail) {
                break; // Queue is empty
            }

            size_t available = tail - head;
            size_t batch_size = std::min(packets.size() - dequeued_count, available);
            
            if (batch_size == 0) {
                backoff();
                continue;
            }

            if (head_seq_.compare_exchange_weak(head, head + batch_size,
                                               std::memory_order_acq_rel,
                                               std::memory_order_acquire)) {
                // Successfully reserved slots
                for (size_t i = 0; i < batch_size; ++i) {
                    Slot& slot = buffer_[(head + i) & mask_];
                    
                    // Wait for slot to have data
                    while (slot.seq.load(std::memory_order_acquire) != head + i + 1) {
                        std::this_thread::yield();
                    }
                    
                    packets[dequeued_count + i] = std::move(slot.packet);
                    slot.seq.store(head + i + capacity_, std::memory_order_release);
                }
                dequeued_count += batch_size;
                backoff.reset();
            } else {
                backoff();
            }
        }
        return dequeued_count;
    }

    // Non-blocking try variants
    bool try_enqueue(const Packet& packet) noexcept {
        size_t tail = tail_seq_.load(std::memory_order_relaxed);
        Slot& slot = buffer_[tail & mask_];
        size_t seq = slot.seq.load(std::memory_order_acquire);
        
        if (seq == tail && tail_seq_.compare_exchange_strong(tail, tail + 1,
                                                            std::memory_order_relaxed,
                                                            std::memory_order_relaxed)) {
            slot.packet = packet;
            slot.seq.store(tail + 1, std::memory_order_release);
            return true;
        }
        return false;
    }

    std::optional<Packet> try_dequeue() noexcept {
        size_t head = head_seq_.load(std::memory_order_relaxed);
        Slot& slot = buffer_[head & mask_];
        size_t seq = slot.seq.load(std::memory_order_acquire);
        
        if (seq == head + 1 && head_seq_.compare_exchange_strong(head, head + 1,
                                                                std::memory_order_relaxed,
                                                                std::memory_order_relaxed)) {
            Packet packet = std::move(slot.packet);
            slot.seq.store(head + capacity_, std::memory_order_release);
            return packet;
        }
        return std::nullopt;
    }

    // Queue state queries
    size_t size() const noexcept {
        size_t tail = tail_seq_.load(std::memory_order_acquire);
        size_t head = head_seq_.load(std::memory_order_acquire);
        return tail - head;
    }

    size_t capacity() const noexcept {
        return capacity_;
    }

    bool empty() const noexcept {
        return size() == 0;
    }

    bool full() const noexcept {
        return size() >= capacity_;
    }

    // Statistics access
    const QueueStats& get_stats() const noexcept {
        return stats_;
    }

    void reset_stats() noexcept {
        stats_.reset();
    }

    // Memory usage estimation
    size_t memory_usage() const noexcept {
        return sizeof(*this) + (capacity_ * sizeof(Slot));
    }
};
