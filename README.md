# Mini-Redis

A high-performance, multi-threaded, RESP-compatible in-memory key-value store written in modern C++17 for Linux.

Mini-Redis is architected around a strict abstraction boundary (`KVBackend`) that allows the server to run interchangeably over two production-grade concurrency control engines:
- **16,384-Way Striped Pessimistic Locking Backend** (`LockingBackend`)
- **Multi-Version Concurrency Control Backend** (`MVCCBackend`) with lock-free CAS writes, snapshot isolation, and 3-epoch reclamation (EBR)

Both storage engines deliver identical observable Redis semantics to upper layers while executing under distinct synchronization paradigms.

---

## Architecture Overview

```
                        +----------------------------+
                        |   Redis Clients (RESP v2)  |
                        +----------------------------+
                                      |
                                      v
                        +----------------------------+
                        |  Linux epoll Reactor Loop  |
                        +----------------------------+
                                      |
                                      v
                        +----------------------------+
                        |  Pinned Worker Thread Pool |
                        |    (CPU Core Affinity)     |
                        +----------------------------+
                                      |
                                      v
                        +----------------------------+
                        |    RESP Command Router     |
                        +----------------------------+
                                      |
                     [ Abstract KVBackend Interface ]
                                      |
             +------------------------+------------------------+
             |                                                 |
             v                                                 v
+--------------------------+                     +--------------------------+
|  Striped Locking Engine  |                     |       MVCC Engine        |
|  - 16,384 rwlock stripes |                     |  - Lock-free CAS writes  |
|  - FNV-1a hash masking   |                     |  - Snapshot-isolated read|
|  - Zero deadlocks        |                     |  - 1,024-shard directory |
|  - Lazy TTL expiration   |                     |  - 3-Epoch EBR memory gate|
+--------------------------+                     +--------------------------+
```

---

## Core Abstraction Boundary (`KVBackend`)

Defined in [`include/kv_backend.h`](include/kv_backend.h), `KVBackend` establishes the decoupling contract between the networking/server runtime and the underlying storage engines. All upper layers interact exclusively through this interface without exposing backend-specific internals such as locks, atomics, epochs, version chains, or memory buckets.

### Operations Contract & Status Codes

| Operation | Signature | Semantics & Status Codes |
|:---|:---|:---|
| **`SET`** | `Set(key, value, expire_ms = -1)` | Inserts or overwrites `key` with `value`. If `expire_ms >= 0`, configures a millisecond TTL deadline (`expire_ms == 0` expires immediately). Replaces any previous value and expiration. Returns `KVStatus::kSetOk`. |
| **`GET`** | `Get(key)` | Retrieves value for `key`. Returns `KVStatus::kGetFound` with the string payload. If the key does not exist or has expired, returns `KVStatus::kGetNotFound` identically (callers cannot distinguish missing from expired). |
| **`DEL`** | `Del(key)` | Removes `key`. Returns `KVStatus::kDelOk` if the key was present and deleted, or `KVStatus::kDelNotFound` if absent or expired (well-defined outcome, not an error). |
| **`EXPIRE`** | `Expire(key, ttl_ms)` | Sets or updates a relative TTL (in milliseconds) on an existing key. Returns `KVStatus::kExpireOk` on success, or `KVStatus::kExpireNotFound` if absent or already expired. |

---

## Networking & Protocol Stack

- **Non-blocking `epoll` Reactor:** Driven by Linux `epoll` edge-triggered/level-triggered events to multiplex thousands of concurrent client TCP connections within an asynchronous reactor loop.
- **Pinned Worker Thread Pool:** A fixed-size worker pool utilizing native POSIX threads (`pthread`) pinned to specific CPU cores via thread affinity (`pthread_setaffinity_np`), maximizing L1/L2/L3 cache locality and eliminating scheduler migration jitter.
- **RESP v2 Parser & Encoder:** High-throughput streaming parser supporting Redis Serialization Protocol (RESP v2) primitives (Simple Strings, Bulk Strings, Integers, Arrays, and Errors). Built with arithmetic overflow checks against 64-bit integer overflows during timestamp and TTL decoding.
- **Connection Isolation & Resilience:** Implements `MSG_NOSIGNAL` socket transmission to avoid process termination via `SIGPIPE` upon abrupt client disconnects, alongside deferred file-descriptor recycling to prevent race conditions during rapid reconnects.

---

## Concurrency Control Engines

### 1. Striped Pessimistic Locking Backend (`LockingBackend`)

Implemented in [`include/backends/locking_backend.h`](include/backends/locking_backend.h) and [`src/backends/locking_backend.cpp`](src/backends/locking_backend.cpp):
- **16,384-Way Striping:** The keyspace is partitioned across 16,384 independent reader/writer lock stripes (`pthread_rwlock_t`), providing high concurrency under multi-threaded read and write workloads.
- **Deterministic FNV-1a Hashing:** Keys are mapped to stripe indices using 64-bit FNV-1a hashing with power-of-two bitwise masking:
  $$\text{stripe\_index} = \text{FNV-1a}(key) \ \& \ (16384 - 1)$$
- **Guaranteed Deadlock Freedom:** Every operation acquires at most a single stripe lock for its entire duration. Cyclic lock dependency graphs cannot form, rendering deadlocks mathematically impossible.
- **Read/Write Concurrency:** `GET` operations acquire shared read locks (`pthread_rwlock_rdlock`), enabling non-blocking concurrent reads within the same stripe. `SET`, `DEL`, and `EXPIRE` acquire exclusive write locks (`pthread_rwlock_wrlock`).
- **Lazy Expiration:** Evaluates TTL deadlines lazily on key access; expired entries encountered during reads or writes are purged immediately under appropriate locks.
- **ThreadSanitizer Clean:** Verified with zero data races under heavy multi-threaded stress tests on shared hot keys.

### 2. Multi-Version Concurrency Control Backend (`MVCCBackend`)

Implemented in [`include/backends/mvcc_backend.h`](include/backends/mvcc_backend.h) and [`src/backends/mvcc_backend.cpp`](src/backends/mvcc_backend.cpp):
- **Per-Key Version Chains:** Each key maintains an atomic head pointer (`std::atomic<Version*>`) referencing a reverse singly-linked chain of immutable version nodes.
- **Lock-Free CAS Writes:** Mutating operations (`SET`, `DEL`, `EXPIRE`) construct a new version and publish it atomically via `compare_exchange_weak`/`compare_exchange_strong` on the slot's head pointer. Writers never acquire coarse data locks.
- **Snapshot-Isolated Reads:** `GET` operations capture a snapshot timestamp and traverse the version chain backward to locate the latest version visible at or before that timestamp. **Readers never block writers, and writers never block readers.**
- **Sharded Key Directory:** Key lookup is partitioned across 1,024 independent directory shards, eliminating global mutex contention during key resolution and insertion.
- **Quiescent-Point Chain Trimming:** To prevent readers from experiencing "false misses" during concurrent version publications, version chains are trimmed exclusively during quiescent points rather than greedily at write time.
- **Three-Epoch EBR Reclamation:** Memory from detached or retired versions is safely reclaimed using an Epoch-Based Reclamation (EBR) scheme. A `std::shared_mutex` quiescence gate eliminates Time-of-Check to Time-of-Use (TOCTOU) hazards: readers and writers hold shared access, while epoch retirement acquires exclusive access only when zero operations are active in that epoch.
- **ThreadSanitizer Verified:** Proven free of data races and use-after-free conditions under multi-threaded reader/writer contention.

---

## Supported Redis Commands

| Command | Syntax | Description |
|:---|:---|:---|
| **PING** | `PING` | Health check; responds with `+PONG`. |
| **SET** | `SET <key> <value> [PX <milliseconds>]` | Sets key to value with optional relative expiration in milliseconds. |
| **GET** | `GET <key>` | Gets value of key. Returns `$-1` (bulk null) if missing or expired. |
| **DEL** | `DEL <key>` | Deletes key. Returns integer count of removed keys (`:1` or `:0`). |
| **EXPIRE** | `EXPIRE <key> <milliseconds>` | Sets TTL in milliseconds. Returns `:1` on success, `:0` if missing/expired. |

---

## Repository Structure

```
.
├── include/
│   ├── kv_backend.h                 # Core abstract storage engine interface
│   └── backends/
│       └── locking_backend.h        # Striped Locking backend declarations
├── src/
│   ├── CMakeLists.txt               # Source target declarations
│   ├── main.cpp                     # Server executable entry point
│   ├── backends/
│   │   └── locking_backend.cpp      # Striped Locking backend implementation
│   ├── network/
│   │   ├── epoll_reactor.h/.cpp     # Non-blocking epoll event reactor
│   │   └── tcp_server.h/.cpp        # Asynchronous TCP server lifecycle
│   ├── protocol/
│   │   ├── resp_parser.h/.cpp       # Streaming RESP protocol deserializer
│   │   └── resp_encoder.h/.cpp      # RESP protocol response serializer
│   ├── router/
│   │   └── command_router.h/.cpp    # Request dispatching & command routing
│   └── thread/
│       ├── thread_pool.h/.cpp       # Pinned worker thread pool
│       └── cpu_affinity.h/.cpp      # CPU core binding utilities
├── tests/
│   ├── CMakeLists.txt               # Test target definitions
│   ├── kv_backend_test.cpp          # Abstract contract unit tests
│   └── locking/
│       ├── CMakeLists.txt           # Locking backend test configuration
│       ├── locking_backend_test.cpp # Semantic, TTL, and idempotency tests
│       ├── concurrency_test.cpp     # Multi-threaded stress & race tests
│       └── TSAN.md                  # ThreadSanitizer verification documentation
└── CMakeLists.txt                   # Top-level build configuration
```

---

## Build & Installation

### Prerequisites

- **Operating System:** Linux (native or WSL2 on Windows)
- **Compiler:** Modern C++17 compliant compiler (`g++` >= 9.0 or `clang++` >= 10.0)
- **Build System:** CMake >= 3.16 and `make` or `ninja`
- **Libraries:** POSIX Threads (`libpthread`)

### Standard Release Build

```bash
# Configure the build directory
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

# Compile all libraries and executables
cmake --build build -j$(nproc)
```

---

## Running the Server

Start the Mini-Redis server:

```bash
./build/src/mini-redis
```

The server binds to port `6379` by default. You can connect using any standard Redis client, such as `redis-cli`, or via `nc`:

```bash
# Using redis-cli
redis-cli -p 6379 PING
redis-cli -p 6379 SET user:1001 "Alice" PX 60000
redis-cli -p 6379 GET user:1001
redis-cli -p 6379 EXPIRE user:1001 30000
redis-cli -p 6379 DEL user:1001

# Using netcat / raw RESP
printf "*3\r\n\$3\r\nSET\r\n\$4\r\ntest\r\n\$5\r\nhello\r\n" | nc 127.0.0.1 6379
```

---

## Testing & Concurrency Verification

### Running the Test Suite

Run unit and integration tests via `ctest`:

```bash
ctest --test-dir build --output-on-failure
```

To run individual test suites directly:

```bash
# Abstract contract tests
./build/tests/kv_backend_test

# Locking backend semantics and TTL tests
./build/tests/locking/locking_backend_test

# Multi-threaded concurrency and stress tests
./build/tests/locking/locking_concurrency_test
```

### ThreadSanitizer (TSan) Verification

Mini-Redis is engineered for high-concurrency safety. Build with ThreadSanitizer to verify the absence of data races, deadlocks, and use-after-free errors:

```bash
# Configure separate TSan build
cmake -S . -B build-tsan \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=thread -fno-omit-frame-pointer -g" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread"

# Build with TSan instrumentation
cmake --build build-tsan -j$(nproc)

# Run test suite under ThreadSanitizer
ctest --test-dir build-tsan --output-on-failure
```

The concurrency suite subjects the backend to:
- **Shared Key Contention:** 8 worker threads concurrently issuing `SET`, `GET`, `DEL`, and `EXPIRE` on the same key.
- **Disjoint Stripe Stress:** Multi-threaded operations dispersed across distinct stripes.
- **Three-Way Race:** Concurrent threads racing `EXPIRE`, `SET`, and `GET` simultaneously.
- **Randomized Monte Carlo Stress:** High-iteration randomized workloads testing idempotency and torn-write prevention.
