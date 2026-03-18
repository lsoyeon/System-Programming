# Concurrent Stock Server — Architecture Comparison & Optimization

A concurrent stock trading server implemented in C (Linux, POSIX), exploring **three concurrency architectures** and measuring their performance trade-offs with real experiments.

---

## 🏗️ Project Structure

```
├── task_1/             # Step 1: Event-Driven Server (select-based)
├── task_1_exp/         # Step 1 + experiment instrumentation
├── task_2/             # Step 2: Thread-Based Server (global mutex)
├── task_2_exp/         # Step 2 + experiment instrumentation
├── task_3_exp/         # Step 3: Thread-Based + Node-level RW Lock
├── exp_event/          # Additional: Optimized Event-Driven
├── exp_rwlock/         # Additional: Fine-grained RWLock experiments
├── exp_graceful/       # Additional: Graceful Shutdown (SIGINT safe)
├── run_exp.py          # Automated benchmark script
└── run_exp2.py         # R/W ratio sweep benchmark
```

## 📋 Architecture Overview

### 1. Event-Driven Server (`task_1`)
- Single-threaded, `select()` based I/O multiplexing
- `Pool` struct manages up to 1024 client FDs
- **Zero lock overhead** — no shared state contention

### 2. Thread-Based Server — Global Mutex (`task_2`)
- Pre-threaded (105 workers), Producer-Consumer pattern via `sbuf` (bounded buffer)
- Master thread `accept()`s → pushes `connfd` to buffer → Workers consume
- **Global mutex** wrapping entire `eval()` — safe but serialized
- 💡 105 threads existed, but only 1 could run at a time → **worse than single-threaded**

### 3. Thread-Based Server — Node-level R/W Lock (`task_3_exp`)
- Each BST node has its own `mutex` + `w_mutex` (1st Readers-Writers Problem)
- `show` (read): increments `readcnt`, first reader acquires `w_mutex` → **multiple readers in parallel**
- `buy/sell` (write): acquires node's `w_mutex` → exclusive access **only on that node**
- Other nodes remain accessible → true fine-grained parallelism

### Additional Experiments
- **`exp_rwlock`**: Further fine-grained RWLock experiments
- **`exp_graceful`**: Safe `SIGINT` handling — `volatile sig_atomic_t shutdown_flag` prevents half-written BST on shutdown
- **`exp_event`**: Optimized event-driven with improved buffer management

---

## 🧪 Measured Experiment Results

### Core Comparison (50 clients × 500 requests, Mixed Workload)

| Architecture | Avg Latency | p99 Tail Latency | vs Event-Driven |
|---|---|---|---|
| Event-Driven (single-thread) | **965 µs** | **11,837 µs** | Baseline |
| Thread + Global Mutex | 2,702 µs | 17,599 µs | ❌ 2.8× slower (avg), 1.5× worse (p99) |
| **Thread + Node-level RW Lock** | **92 µs** | **1,030 µs** | ✅ **10.5× faster (avg), 11.5× faster (p99)** |

**Key Finding**: Global mutex serializes all 105 threads → worse than single-threaded. Node-level RW Lock unlocks true parallelism.

### Read/Write Ratio Sweep (p99 Tail Latency)

| Show (Read) % | Event-Driven p99 | Thread + RW Lock p99 | RW Lock Advantage |
|---|---|---|---|
| 0% (write only) | 10,610 µs | **877 µs** | 12.1× faster |
| 25% | 10,226 µs | **1,073 µs** | 9.5× faster |
| 50% | 3,724 µs | **823 µs** | 4.5× faster |
| 75% | 7,336 µs | **593 µs** | 12.4× faster |
| 100% (read only) | 11,855 µs | **819 µs** | 14.5× faster |

**Key Finding**: With node-level locks, Thread+RWLock dominates at **all R/W ratios** — even write-only workloads. Lock granularity matters more than workload type.

### Graceful Shutdown Verification
- 20 clients trading simultaneously → `SIGINT` sent mid-operation
- Post-shutdown `stock.txt` validation: **all stock IDs preserved (PASS), all prices preserved (PASS)**
- Data integrity: 100% maintained

---

## 🔍 Debugging Journey

1. **Initial assumption**: "More threads = faster" → Thread server was 2.8× slower
2. **Wrong hypotheses tested**: Removed printf I/O, reduced buffer size, increased requests → None worked
3. **Root cause found**: Global mutex around `eval()` → 105 threads serialized + context switch overhead
4. **Verification**: `usleep(10ms)` placed **outside lock block** proved parallelism exists when lock scope is narrow
5. **Solution**: Node-level Readers-Writers Lock → **10.5× avg, 11.5× p99 improvement**

---

## 🔧 Build & Run

**Requirements**: Linux (or WSL), GCC, POSIX threads

```bash
# Build (in any task directory)
cd task_3_exp
make

# Run server (terminal 1)
./stockserver 12345

# Run test client (terminal 2)
./multiclient 12345 50 500
# Args: port, num_clients, requests_per_client
```

## 📊 Running Benchmarks

```bash
# Basic benchmark (Event vs Thread vs RWLock)
python3 run_exp.py

# R/W ratio sweep (0%, 25%, 50%, 75%, 100% read)
python3 run_exp2.py
```

## 🗄️ Data Model

Stock data stored in a **Binary Search Tree (BST)**:
- `show`: Inorder traversal → O(N) — read lock per node
- `buy/sell`: Binary search → O(log N) — write lock on target node only
- Persistent: loaded from / saved to `stock.txt`

## 💡 Key Takeaways

1. **"More threads ≠ faster"** — Lock contention with global mutex made 105 threads *slower* than 1 thread
2. **Lock granularity > thread count** — Node-level RW Lock achieved 10.5× improvement without changing thread count
3. **Measure before optimizing** — 3 failed hypotheses taught me to verify assumptions with data, not intuition
4. **Fine-grained locks win at all R/W ratios** — When lock scope is narrow enough, multithreading dominates regardless of workload mix
5. **Systems must be safe at shutdown** — Graceful `SIGINT` handling preserves data integrity during mid-transaction termination
