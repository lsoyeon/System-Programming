# Concurrent Stock Server — Architecture Comparison & Optimization

A concurrent stock trading server implemented in C (Linux, POSIX), exploring **three concurrency architectures** and measuring their performance trade-offs with real experiments.

---

## 🏗️ Project Structure

```
├── task_1/             # Step 1: Event-Driven Server (select-based)
├── task_1_exp/         # Step 1 + experiment instrumentation
├── task_2/             # Step 2: Thread-Based Server (global mutex)
├── task_2_exp/         # Step 2 + experiment instrumentation
├── task_3_exp/         # Step 3: Thread-Based + Reader-Writer Lock
├── exp_event/          # Additional: Optimized Event-Driven
├── exp_rwlock/         # Additional: Fine-grained RWLock per node
├── exp_graceful/       # Additional: Graceful Shutdown (SIGINT safe)
├── run_exp.py          # Automated benchmark script
└── run_exp2.py         # Extended benchmark (R/W ratio sweep)
```

## 📋 Architecture Overview

### 1. Event-Driven Server (`task_1`)
- Single-threaded, `select()` based I/O multiplexing
- `Pool` struct manages up to 1024 client FDs
- **Zero lock overhead** — no shared state contention
- Best for **read-heavy** workloads

### 2. Thread-Based Server (`task_2`)
- Pre-threaded (105 workers), Producer-Consumer pattern via `sbuf` (bounded buffer)
- Master thread `accept()`s → pushes `connfd` to buffer → Workers consume
- **Global mutex** wrapping entire `eval()` — safe but serialized
- Initial expectation: "more threads = faster" → **proved wrong**

### 3. Thread-Based + Reader-Writer Lock (`task_3_exp`)
- Replaced global mutex with `pthread_rwlock_t`
- `show` (read) commands acquire **read lock** → multiple readers in parallel
- `buy/sell` (write) commands acquire **write lock** → exclusive access
- Performance scales with read-heavy workloads

### Additional Experiments
- **`exp_rwlock`**: Fine-grained RWLock (per BST node) for maximum parallelism
- **`exp_graceful`**: Safe `SIGINT` handling — `volatile sig_atomic_t shutdown_flag` instead of immediate `exit(0)`, preventing half-written BST state
- **`exp_event`**: Optimized event-driven with improved buffer management

## 🧪 Key Experimental Results

| Architecture | Avg Latency | p99 Tail Latency | Notes |
|---|---|---|---|
| Event-Driven | **965 µs** | **11.8 ms** | No lock overhead |
| Thread (Global Mutex) | 2,702 µs | 17.5 ms | 2.8× slower than Event |
| Thread (RWLock) | ~1,100 µs | ~12 ms | Competitive with Event on read-heavy |

**Key Insight**: Global mutex serializes all 105 threads → worse than single-threaded. The bottleneck was **lock contention**, not CPU.

## 🔧 Build & Run

**Requirements**: Linux (or WSL), GCC, POSIX threads

```bash
# Build (in any task directory)
cd task_1
make

# Run server
./stockserver 12345

# Run test client (in another terminal)
./multiclient 12345 50 500
# Args: port, num_clients, requests_per_client
```

## 📊 Running Experiments

```bash
# Basic benchmark
python3 run_exp.py

# R/W ratio sweep (0%, 25%, 50%, 75%, 100% read)
python3 run_exp2.py
```

## 🗄️ Data Model

Stock data stored in a **Binary Search Tree (BST)**:
- `show`: Inorder traversal → O(N)
- `buy/sell`: Binary search → O(log N)
- Persistent: loaded from / saved to `stock.txt`

## 💡 What I Learned

1. **"More threads ≠ faster"** — Lock contention can make multithreading *slower* than single-threaded
2. **Architecture choice should be data-driven** — R/W ratio determines the break-even point between Event-Driven and Thread-Based
3. **Reader-Writer Lock is the key** — When reads dominate (e.g., `show` commands), RWLock allows true parallelism while mutex serializes everything
4. **Graceful shutdown matters** — `SIGINT` during `buy/sell` can corrupt BST state; flag-based shutdown prevents data loss
