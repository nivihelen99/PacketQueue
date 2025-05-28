# PacketQueue



# 🚀 Lock-Free PacketQueue for Multi-Threaded Packet Processing

A high-performance, thread-safe packet queue written in modern C++ designed for use in Layer 2 switch/router software. Built for low-latency, high-throughput environments with lock-free concurrency primitives.

## 📌 Features

- 🔄 **Lock-Free Circular Queue**:
  - Designed using `std::atomic` and memory orderings.
  - Single-Producer Single-Consumer (SPSC) and extendable to Multi-Producer Multi-Consumer (MPMC).

- ⚡ **High Performance**:
  - Cache-aligned data structures to minimize false sharing.
  - Batch enqueue/dequeue support for better throughput.

- 🧵 **Thread-Safe Packet Buffering**:
  - Store raw buffer pointers with metadata (length, priority).
  - Prioritization support for Quality of Service (QoS).

- 🧠 **Memory Efficiency**:
  - Fixed-size ring buffer avoids dynamic memory allocations.
  - Ready to integrate with external memory/pool allocators.

## 🛠 Use Cases

- Ingress/Egress buffer queues per port
- Work distribution across cores (e.g., RX to processing threads)
- QoS prioritization (e.g., control vs data packet queues)
- Inter-thread packet communication in pipeline stages

## 📦 Example Packet Structure

```cpp
struct Packet {
    uint8_t* buffer;
    size_t length;
    uint8_t priority;
};
````

## 🧪 Example Usage

```cpp
PacketQueue q;

uint8_t* buf = ...; // from memory pool
q.enqueue(buf, 128, 1); // High-priority control packet

Packet pkt;
if (q.dequeue(pkt)) {
    process(pkt.buffer, pkt.length);
}
```

## 🔧 Build & Test

```bash
g++ -std=c++20 -O3 -pthread -o packet_queue main.cpp
./packet_queue
```

## 🚧 Roadmap

* [ ] Add MPMC version with CAS
* [ ] Add batch enqueue/dequeue API
* [ ] Integrate with memory pool (custom allocator)
* [ ] Benchmark with real packet loads
* [ ] Extend to allow priority queuing via multiple queues

## 📄 License



---



