# Mini-Redis: Phase 1 Networking & Concurrency Architecture

**Author:** Smriti Bisht (Team Lead, Team T201 — Fantastic Four)  
**Components Owned:** Epoll Reactor, Non-blocking TCP Server, RESP Protocol Engine, Pinned Pthread Worker Pool, Command Dispatch & Shared Backend Interface

---

## 1. Architectural Overview

Mini-Redis departs from the standard Redis single-threaded reactor bottleneck by implementing a high-throughput, multi-threaded request processing pipeline designed for multi-core hardware.

```
+-------------------------------------------------------------------+
|                        Client TCP Connections                     |
+-------------------------------------------------------------------+
                                  |
                                  v
+-------------------------------------------------------------------+
|            Edge-Triggered Epoll / Kqueue Reactor (I/O Loop)       |
|  - Non-blocking I/O (O_NONBLOCK, SO_REUSEADDR, TCP_NODELAY)       |
|  - Edge-triggered event demultiplexing (EPOLLET / EV_CLEAR)       |
|  - Drain read/write buffers until EAGAIN / EWOULDBLOCK            |
+-------------------------------------------------------------------+
                                  |
                                  v
+-------------------------------------------------------------------+
|                  RESP Parser & Protocol Framer                    |
|  - Incremental chunk parser for fragmented frames                 |
|  - Pipelined batch request decomposition                          |
|  - Strict validation of RESP array/bulk wire formats              |
+-------------------------------------------------------------------+
                                  |
                                  v
+-------------------------------------------------------------------+
|                  Pinned Pthread Worker Pool                       |
|  - Fixed worker threads bound to hardware CPU cores               |
|  - Thread affinity (pthread_setaffinity_np) avoids cache thrash   |
|  - Parallel execution via CommandRouter                           |
+-------------------------------------------------------------------+
                                  |
                                  v
+-------------------------------------------------------------------+
|                   Abstract KVBackend Interface                    |
|             (LockingBackend / MVCCBackend Selectable)             |
+-------------------------------------------------------------------+
```

---

## 2. Edge-Triggered Epoll Demultiplexing & Buffer Draining

### 2.1 The Edge-Triggered Challenge
Edge-triggered notification (`EPOLLET` on Linux / `EV_CLEAR` on Darwin) only alerts the reactor when the state transitions from unready to ready. If any incoming bytes remain buffered in socket kernel queues, the reactor will stall and miss incoming client commands.

### 2.2 Buffer Draining Mechanism
To ensure lockless correctness and eliminate stalling:
- Every `READ` event loops calling `recv()` until the syscall yields `-1` with `errno == EAGAIN` or `errno == EWOULDBLOCK`.
- Incoming fragments append to each client's dedicated `in_buf`.
- Outgoing replies buffer into `out_buf` and are drained in a non-blocking loop. If `send()` yields `EAGAIN`, the descriptor is registered with `WRITE` interest in the reactor until the socket buffer clears.

---

## 3. Pinned Pthread Worker Pool & CPU Affinity

To prevent CPU cache line migration and minimize context-switch latency:
1. **Core Pinning:** At initialization, worker `i` pins its calling thread to CPU core `(i % hardware_cores)` using POSIX affinity primitives.
2. **Task Queueing:** Tasks queue in FIFO order guarded by a condition variable with low lock contention.
3. **Exception Boundary:** All worker tasks execute inside `try/catch` boundaries ensuring that faulty or malformed requests never bring down the worker threads.

---

## 4. Test & Verification Coverage

| Test Suite | File | Coverage | Status |
| :--- | :--- | :--- | :--- |
| **KVBackend Contract** | `tests/kv_backend_test.cpp` | SET, GET, DEL, EXPIRE semantics across backends | PASS |
| **ThreadPool & Affinity** | `tests/test_threadpool.cpp` | Worker pinning, concurrent burst (1000 tasks), future returns, exception isolation | PASS |
| **Epoll Reactor** | `tests/test_epoll.cpp` | Non-blocking socket setup, edge-triggered drain loops, event polling | PASS |
| **RESP Protocol** | `tests/test_resp.cpp` | Byte-by-byte fragmented packets, pipelined batches, error formats | PASS |
| **Command Router** | `tests/test_router.cpp` | Command arity, PX ms conversion, missing key semantics | PASS |
| **TCP Integration** | `tests/test_tcp_integration.cpp` | Full TCP client round-trip, multi-client concurrency, lazy TTL over wire | PASS |
