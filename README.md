<div align="center">

# ⚡ Mini-Redis

**A High-Performance, Multi-Threaded In-Memory Key-Value Store in Modern C++17**  
*Evaluating Fine-Grained Striped Locking vs. Lock-Free MVCC Behind an Invariant Storage Boundary*

[![C++17](https://img.shields.io/badge/Standard-C%2B%2B17-00599C.svg?style=flat-square&logo=c%2B%2B)](https://en.cppreference.com/w/cpp/17)
[![Platform](https://img.shields.io/badge/Platform-Linux%20%7C%20WSL2-FCC624.svg?style=flat-square&logo=linux&logoColor=black)](https://www.kernel.org/)
[![Build](https://img.shields.io/badge/Build-CMake%20%E2%89%A5%203.16-064F8C.svg?style=flat-square&logo=cmake)](https://cmake.org/)
[![Sanitizer](https://img.shields.io/badge/TSan-Zero%20Data%20Races-success.svg?style=flat-square)](tests/locking/TSAN.md)
[![Throughput](https://img.shields.io/badge/Peak%20Throughput-8.64M%20ops%2Fsec-red.svg?style=flat-square)](#-empirical-benchmarks--performance)
[![License](https://img.shields.io/badge/License-MIT-green.svg?style=flat-square)](LICENSE)

</div>

---

## 📖 Executive Summary

**Mini-Redis** is a production-grade, multi-core in-memory key-value database built from first principles in modern C++17 for Linux. It is engineered to answer a fundamental systems research question:

> *When surrounded by identical non-blocking I/O multiplexing, CPU core affinity, memory allocators, and wire framing, does optimistic Multi-Version Concurrency Control (MVCC) truly outperform fine-grained Striped Pessimistic Locking under skewed multi-core workloads?*

To isolate concurrency control from orthogonal variables, Mini-Redis enforces an invariant, decoupled storage abstraction boundary (`KVBackend`). Behind this boundary, the server can switch interchangeably between two industrial-strength concurrency engines:

1. **16,384-Way Striped Reader-Writer Locking Engine (`LockingBackend`)**: Bitwise power-of-two FNV-1a hashing, single-stripe acyclic lock graph (provably deadlock-free), shared read locks, and in-lock lazy TTL expiration.
2. **Lock-Free Multi-Version Concurrency Control Engine (`MVCCBackend`)**: Atomic CAS version chain publication, non-blocking snapshot-isolated reads, 1,024-shard directory, and a 3-epoch Epoch-Based Reclamation (EBR) quiescence gate.

---

## 🚀 Key Highlights & Research Findings

<table>
  <tr>
    <td width="50%">
      <h3>⚡ 8.64 Million ops/sec Peak</h3>
      Under 100% read workloads at 8 cores, Striped Locking delivers <b>8,635,573 ops/sec</b> with <b>96.7% parallel efficiency</b>, outperforming MVCC by <b>+40.1%</b> due to zero cross-core cache invalidation storms.
    </td>
    <td width="50%">
      <h3>🛡️ Zero Deadlocks by Proof</h3>
      Every operation maps to exactly one stripe and acquires at most one <code>pthread_rwlock_t</code>. The lock acquisition graph is strictly acyclic, making deadlocks mathematically impossible.
    </td>
  </tr>
  <tr>
    <td>
      <h3>⏱️ 14.7× Lower Tail Latency</h3>
      Under 50% write contention at 8 cores, Striped Locking maintains a predictable <b>3.0 µs p99 latency</b>, whereas MVCC degrades to <b>44.0 µs p99</b> due to version allocation stalls and EBR synchronization.
    </td>
    <td>
      <h3>🧼 ThreadSanitizer Clean</h3>
      Verified with zero data races, zero deadlocks, and zero memory leaks under multi-threaded Monte Carlo stress workloads hammering shared keys.
    </td>
  </tr>
</table>

---

## 👥 Research Team & Authors

Mini-Redis was conceived, engineered, and empirically benchmarked by the **Systems Research Group**, Department of Computer Science & Engineering (*OS & DBMS Laboratory*):

| Author | Phase & Focus | Key Systems Contributions |
|:---|:---|:---|
| **Yash Aeron** | **Phase 2A** — Striped Locking Engine | 16,384-way `pthread_rwlock_t` striping; FNV-1a bitwise hash masking; single-stripe deadlock freedom proof; multi-threaded Monte Carlo race test harness; ThreadSanitizer concurrency audit. |
| **Smriti Bisht** | **Phase 0 & 1** — Core Scaffold & Networking | Abstract `KVBackend` decoupling contract; Linux `epoll` reactor event loop; pinned POSIX thread pool (`pthread_setaffinity_np`); streaming RESP v2 parser/serializer. |
| **Atul Negi** | **Phase 2B** — Lock-Free MVCC Backend | Atomic CAS version chains (`compare_exchange_weak`); snapshot-isolated read path; 1,024-shard lock-free directory; 3-Epoch EBR memory gate (`std::shared_mutex`). |
| **Priyanshu Kathait** | **Phase 3 & 4** — Persistence & Benchmarking | Append-Only File (AOF) binary logging; master-replica replication engine; Zipfian-skewed benchmark driver ($\theta = 0.99$); high-precision reservoir sampling telemetry. |

---

## 🏗️ System Architecture

```
                                 +----------------------------+
                                 |   Redis Clients (RESP v2)  |
                                 +----------------------------+
                                               |
                                               v
                                 +----------------------------+
                                 |  Linux epoll Reactor Loop  |
                                 |  - Edge/Level Triggered    |
                                 |  - Non-blocking TCP Server |
                                 +----------------------------+
                                               |
                                               v
                                 +----------------------------+
                                 |  Pinned Worker Thread Pool |
                                 |  - pthread_setaffinity_np  |
                                 |  - L1/L2 Cache Locality    |
                                 +----------------------------+
                                               |
                                               v
                                 +----------------------------+
                                 |    RESP Command Router     |
                                 |  - PING / SET / GET / DEL  |
                                 |  - Millisecond TTL parsing |
                                 +----------------------------+
                                               |
                              [ Invariant KVBackend Interface ]
                                               |
                      +------------------------+------------------------+
                      |                                                 |
                      v                                                 v
         +--------------------------+                     +--------------------------+
         |  Striped Locking Engine  |                     |       MVCC Engine        |
         |  - 16,384 rwlock stripes |                     |  - Lock-free CAS writes  |
         |  - Bitwise FNV-1a mask   |                     |  - Snapshot-isolated read|
         |  - Zero deadlocks        |                     |  - 1,024-shard directory |
         |  - In-lock lazy TTL exp. |                     |  - 3-Epoch EBR memory gate|
         +--------------------------+                     +--------------------------+
```

### The Invariant Storage Contract (`KVBackend`)

Defined in [`include/kv_backend.h`](include/kv_backend.h), `KVBackend` provides complete isolation between the network runtime and the underlying concurrency mechanics:

```cpp
class KVBackend {
 public:
  virtual ~KVBackend() = default;
  virtual KVResult Set(const std::string& key, const std::string& value, int64_t expire_ms = -1) = 0;
  virtual KVResult Get(const std::string& key) = 0;
  virtual KVResult Del(const std::string& key) = 0;
  virtual KVResult Expire(const std::string& key, int64_t ttl_ms) = 0;
};
```

* **Observable Equivalence:** Callers cannot distinguish between missing keys and expired keys (`kGetNotFound`).
* **TTL Mechanics:** `expire_ms == -1` indicates persistent keys; `expire_ms == 0` expires immediately; positive values establish millisecond deadlines from mutation timestamp.
* **Idempotence & Safety:** `Del` on absent/expired keys returns a clean `kDelNotFound` status rather than throwing errors.

---

## ⚙️ Dual Concurrency Engines

### 1. 16,384-Way Striped Locking Engine (`LockingBackend`)

*Header:* [`include/backends/locking_backend.h`](include/backends/locking_backend.h) | *Implementation:* [`src/backends/locking_backend.cpp`](src/backends/locking_backend.cpp)

* **Fine-Grained Striping:** Partitions the key space across $16,384$ ($2^{14}$) independent `pthread_rwlock_t` instances.
* **Single-Cycle Deterministic Hashing:** Maps keys to stripe indices using a 64-bit Fowler–Noll–Vo (FNV-1a) hash with power-of-two bitwise masking:
  $$\text{stripe\_index} = \text{FNV-1a}_{64}(\text{key}) \ \& \ 0\text{x}3\text{FFF}$$
  Replaces slow modulo division with a single-cycle bitwise `AND` operation.
* **Mathematical Deadlock Freedom:** Because all Mini-Redis operations are single-key primitives, each thread acquires at most **one** stripe lock at any given time. The resource allocation graph contains zero cycles:
  $$\text{HeldLocks}(T_i) \cap \text{HeldLocks}(T_j) = \emptyset \implies P(\text{Deadlock}) = 0$$
* **Reader/Writer Concurrency:** `Get` acquires a shared reader lock (`pthread_rwlock_rdlock`), enabling non-blocking concurrent reads within the stripe. `Set`, `Del`, and `Expire` acquire exclusive write locks (`pthread_rwlock_wrlock`).
* **In-Lock Lazy Expiration:** Evaluates TTL deadlines lazily on key access; expired records are pruned immediately under an exclusive lock.

### 2. Lock-Free MVCC Engine (`MVCCBackend`)

*Header:* `include/backends/mvcc_backend.h` | *Implementation:* `src/backends/mvcc_backend.cpp`

* **Per-Key Version Chains:** Each key slot points to an atomic head (`std::atomic<Version*>`) referencing a reverse singly-linked list of immutable version records.
* **Lock-Free Atomic CAS Writes:** Writers construct a new `Version` node and publish it via atomic compare-and-swap (`compare_exchange_weak`). Writers never acquire data locks.
* **Snapshot-Isolated Reads:** Readers capture the global monotonic timestamp at operation start and traverse backwards along the version chain to find the latest version visible at or before their snapshot. Readers never block writers, and writers never block readers.
* **1,024-Shard Directory:** Key slot allocation is partitioned across 1,024 directory shards to eliminate global mutex contention during table expansion.
* **3-Epoch EBR Reclamation:** Memory from retired version records is reclaimed using a 3-epoch Epoch-Based Reclamation system hardened with a `std::shared_mutex` quiescence gate to guarantee safe pointer traversal without Use-After-Free hazards.

---

## 📊 Empirical Benchmarks & Performance

Evaluated across **72 benchmark runs** covering 24 distinct configurations on dedicated hardware ($100,000$ keys, heavy Zipfian access skew $\theta = 0.99$, 1 to 8 pinned physical cores):

### Throughput & Tail Latency Comparison

| Workload Profile | Cores ($p$) | Striped Locking Throughput | MVCC Throughput | Locking Throughput Advantage | Locking $p99$ Latency | MVCC $p99$ Latency |
|:---|:---:|:---:|:---:|:---:|:---:|:---:|
| **100% Read** | 1 | 1.12 M ops/sec | **1.42 M ops/sec** | MVCC +26.8% | 2.0 µs | **1.0 µs** |
| **100% Read** | 2 | 2.44 M ops/sec | **2.55 M ops/sec** | MVCC +4.6% | 1.7 µs | **1.0 µs** |
| **100% Read** | 4 | **4.58 M ops/sec** | 4.37 M ops/sec | **Locking +4.8%** | 2.0 µs | **1.0 µs** |
| **100% Read** | 8 | **8.64 M ops/sec** | 6.16 M ops/sec | **Locking +40.1%** | 2.0 µs | **2.0 µs** |
| **80% Read / 20% Write** | 4 | **3.87 M ops/sec** | 1.44 M ops/sec | **Locking 2.68×** | **2.0 µs** | 3.3 µs |
| **80% Read / 20% Write** | 8 | **2.79 M ops/sec** | 1.85 M ops/sec | **Locking 1.51×** | **3.0 µs** | 4.0 µs |
| **50% Read / 50% Write** | 4 | **3.82 M ops/sec** | 0.82 M ops/sec | **Locking 4.64×** | **2.0 µs** | 7.0 µs |
| **50% Read / 50% Write** | 8 | **2.55 M ops/sec** | 0.84 M ops/sec | **Locking 3.04×** | **3.0 µs** | **44.0 µs** |

### Throughput Scaling Curve (100% Reads)

```
Throughput (M ops/sec)
  9.0 ┤                                                     ● Striped Locking (8.64M ops/sec)
  8.0 ┤                                                    
  7.0 ┤                                                   
  6.0 ┤                                                     ▲ MVCC (6.16M ops/sec)
  5.0 ┤                                  ● Striped Locking (4.58M)
  4.0 ┤                                  ▲ MVCC (4.37M)
  3.0 ┤                ▲ MVCC (2.55M)
  2.0 ┤  ▲ MVCC (1.42M)● Striped Locking (2.44M)
  1.0 ┤  ● Striped Locking (1.12M)
  0.0 ┼───────┬──────────────┬───────────────────┬───────────────
             1 Core         2 Cores             4 Cores         8 Cores
```

### Architectural Takeaways

1. **The Parallel Efficiency Inversion:** While MVCC leads at 1–2 cores due to lock-free pointer traversal, Striped Locking achieves **96.7% parallel efficiency at 8 cores** vs. MVCC's **54.4%**. In MVCC, readers must acquire atomic shared memory barriers (`memory_order_acquire`) on the global version counter and EBR epoch gate, inducing high cross-core MESI bus invalidations.
2. **Write Performance Collapse in MVCC:** Under 50% writes at 8 cores, Striped Locking outperforms MVCC by **3.04× to 4.64×**. Profiling proves this collapse is **not** caused by CAS aborts (aborts remained $< 0.28\%$), but rather by dynamic heap allocation pressure (`new Version`), cache-line thrashing, and EBR quiescence delays.
3. **Memory Footprint:** Striped Locking consumes **~7.2 MB** resident memory, whereas MVCC consumes **~22.6 MB** ($>3.1\times$ footprint) due to multi-version retention and retirement queues.

---

## ⌨️ Supported Redis Commands

Mini-Redis natively implements the Redis Serialization Protocol (RESP v2):

| Command | Wire Syntax | Description & Return Semantics |
|:---|:---|:---|
| `PING` | `PING` | Server liveness probe. Responds with simple string `+PONG\r\n`. |
| `SET` | `SET <key> <value> [PX <ms>]` | Inserts or overwrites key. Optional millisecond TTL (`PX`). Responds with `+OK\r\n`. |
| `GET` | `GET <key>` | Fetches value. Responds with bulk string `$len\r\nvalue\r\n` or null bulk `$-1\r\n` if missing/expired. |
| `DEL` | `DEL <key>` | Removes key. Responds with integer `:1\r\n` (deleted) or `:0\r\n` (not found). |
| `EXPIRE` | `EXPIRE <key> <ms>` | Configures relative millisecond TTL. Responds with `:1\r\n` (success) or `:0\r\n` (missing). |

---

## 🛠️ Quickstart: Build & Run

### Prerequisites
* **OS:** Linux (Ubuntu 20.04+, Debian 11+, RHEL 8+, or WSL2 on Windows)
* **Compiler:** C++17 compatible compiler (`g++` >= 9.0 or `clang++` >= 10.0)
* **Build Tools:** CMake >= 3.16 and `make` / `ninja`
* **Libraries:** POSIX Threads (`libpthread`)

### 1. Build the Release Binary

```bash
# Clone the repository
git clone https://github.com/sumiiesque/Mini-Redis.git
cd Mini-Redis

# Configure and compile in Release mode
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

### 2. Launch the Server

```bash
# Start server (binds to 127.0.0.1:6379 by default)
./build/src/mini-redis
```

### 3. Interact with Standard Clients

Connect using standard `redis-cli`:

```bash
redis-cli -p 6379 PING
# Output: PONG

redis-cli -p 6379 SET user:101 "Ada Lovelace" PX 60000
# Output: OK

redis-cli -p 6379 GET user:101
# Output: "Ada Lovelace"

redis-cli -p 6379 EXPIRE user:101 10000
# Output: (integer) 1

redis-cli -p 6379 DEL user:101
# Output: (integer) 1
```

Or communicate directly via raw TCP socket using `nc` (netcat):

```bash
printf '*3\r\n$3\r\nSET\r\n$4\r\ntest\r\n$5\r\nhello\r\n' | nc 127.0.0.1 6379
# Output: +OK
```

---

## 🧪 Testing & Concurrency Verification

### Unit & Semantic Verification
Run the integrated test suite through `ctest`:

```bash
ctest --test-dir build --output-on-failure
```

Direct execution of dedicated test targets:

```bash
# Locking backend semantics and TTL expiration tests
./build/tests/locking/locking_backend_test

# Multi-threaded concurrency and stress race tests
./build/tests/locking/locking_concurrency_test
```

### ThreadSanitizer (TSan) Verification Suite

Mini-Redis is engineered with zero-tolerance for data races and synchronization bugs. You can compile with full Clang/GCC ThreadSanitizer instrumentation:

```bash
# Configure separate TSan build tree
cmake -S . -B build-tsan \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=thread -fno-omit-frame-pointer -g" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread"

# Compile instrumented binaries
cmake --build build-tsan -j$(nproc)

# Run complete stress harness under ThreadSanitizer
ctest --test-dir build-tsan --output-on-failure
```

**Stress Scenarios Validated:**
* `TestConcurrentSetGetDelSameKey`: 8 concurrent worker threads continuously issuing conflicting mutations on a single hot key.
* `TestConcurrentDifferentStripes`: Multi-threaded parallel access dispersed across disjoint stripes.
* `TestExpireRacingWithGetSet`: 3-way race condition between dedicated `EXPIRE`, `SET`, and `GET` threads for 200ms continuously.
* `TestRandomizedStress`: 8 worker threads executing 5,000 randomized operations over a 64-key space verifying torn-write prevention.

---

## 🎬 Live Evaluator Demonstration Toolkit

For interactive project defenses and live evaluator demonstrations, pre-configured demo scripts are available under [`demo/`](demo/):

* **Master 3-Presenter Script (Full Team Presentation):**
  ```bash
  ./demo/demo_all.sh
  ```
  *Walks sequentially through Smriti, Yash, and Atul's modules with pauses for evaluator interaction.*

* **Individual Presenter Scripts:**
  * **Smriti (Networking & Core Protocol):** `./demo/demo_smriti.sh`
  * **Yash (Striped Locking Engine & Concurrency):** `./demo/demo_yash.sh`
  * **Atul (Lock-Free MVCC & Snapshot Isolation):** `./demo/demo_atul.sh`

* **Hot-Key Concurrency Stress Probe:**
  ```bash
  python3 demo/stress.py --threads 4 --keys 10 --ops 8000
  ```

---

## 📁 Repository Layout

```
Mini-Redis/
├── include/
│   ├── kv_backend.h                 # Core abstract storage engine boundary
│   └── backends/
│       └── locking_backend.h        # Striped Locking backend declarations
├── src/
│   ├── CMakeLists.txt               # Source target definitions
│   ├── main.cpp                     # Server executable entry point
│   ├── backends/
│   │   └── locking_backend.cpp      # Striped Locking backend implementation
│   ├── common/
│   │   ├── command.h                # Internal command representations
│   │   └── response.h               # Internal response primitives
│   ├── network/
│   │   ├── epoll_reactor.h/.cpp     # Non-blocking epoll event reactor
│   │   └── tcp_server.h/.cpp        # Asynchronous TCP server lifecycle
│   ├── protocol/
│   │   ├── resp_parser.h/.cpp       # Streaming RESP protocol parser
│   │   └── resp_encoder.h/.cpp      # Streaming RESP protocol encoder
│   ├── router/
│   │   └── command_router.h/.cpp    # Request dispatching & routing
│   └── thread/
│       ├── thread_pool.h/.cpp       # Pinned POSIX worker thread pool
│       └── cpu_affinity.h/.cpp      # CPU core binding utilities
├── tests/
│   ├── CMakeLists.txt               # Top-level test definitions
│   └── locking/
│       ├── CMakeLists.txt           # Locking backend test rules
│       ├── locking_backend_test.cpp # Semantic, TTL, and idempotency tests
│       ├── concurrency_test.cpp     # Multi-threaded stress & race tests
│       └── TSAN.md                  # ThreadSanitizer audit documentation
├── demo/                            # Live evaluator demonstration scripts
│   ├── demo_all.sh                  # Master 3-presenter evaluation runner
│   ├── demo_smriti.sh               # Networking & protocol verification script
│   ├── demo_yash.sh                 # Concurrency & hot-key stress script
│   ├── demo_atul.sh                 # MVCC lock-free snapshot isolation probe
│   └── stress.py                    # Multi-threaded consistency probe
├── CMakeLists.txt                   # Root build configuration
└── README.md                        # Master project documentation
```

---

## 📜 Academic Citation & License

This research and software is released under the **MIT License**.

If you use Mini-Redis in your research or wish to reference our empirical findings, please cite:

```bibtex
@article{aeron2026miniredis,
  title   = {Striped Locking versus Multi-Version Concurrency Control in a Multi-Core In-Memory Key-Value Store: An Empirical and Methodological Investigation},
  author  = {Aeron, Yash and Bisht, Smriti and Kathait, Priyanshu and Negi, Atul},
  journal = {Systems Research Group, Department of Computer Science \& Engineering},
  year    = {2026}
}
```
