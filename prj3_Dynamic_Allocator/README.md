# Dynamic Memory Allocator — Segregated Free List with Boundary Tag Optimization

A custom `malloc`/`free`/`realloc` implementation in C, built from scratch using **Segregated Free List**, **Best-Fit search**, **O(1) coalescing**, and **footer elimination optimization**.

> Built for the *Systems Programming* course (CS:APP malloc lab). Final score: **94/100**, Memory utilization: **81.3%**.

---

## 🏗️ Architecture

### Block Structure (8-byte aligned)

```
Allocated Block:              Free Block:
┌──────────────┐              ┌──────────────┐
│   Header     │ 4B           │   Header     │ 4B
│ [size|PA|A]  │              │ [size|PA|0]  │
├──────────────┤              ├──────────────┤
│              │              │   prev_ptr   │ 4B
│   Payload    │              ├──────────────┤
│              │              │   next_ptr   │ 4B
│              │              ├──────────────┤
│              │              │   (unused)   │
├──────────────┤              ├──────────────┤
│  (no footer) │ ← saved 4B  │   Footer     │ 4B
└──────────────┘              │ [size|PA|0]  │
                              └──────────────┘
```

**Key optimization**: Allocated blocks have **no footer** — the `prev_alloc` (PA) bit is stored in the next block's header, saving 4 bytes per allocated block.

### Segregated Free List (16 size classes)

```
free_array[0]  → blocks of size 16        (2⁴)
free_array[1]  → blocks of size 17~32     (2⁵)
free_array[2]  → blocks of size 33~64     (2⁶)
...
free_array[15] → blocks of size > 2¹⁹
```

- `malloc()`: Compute size class index via bit-shift → search only that list → **Best-Fit** within the class
- If perfect fit found → return immediately (early exit)
- If no fit in target class → search larger classes

---

## ⚙️ Core Algorithms

### 1. `mm_malloc(size)` — Segregated Best-Fit Allocation
1. Align requested size to 8-byte boundary: `ALIGN(size + WSIZE)`
2. Find size class index: `find_idx(asize)` using bit-shift comparison
3. Search free list for best fit (smallest block ≥ requested size)
4. If no fit → `extend_heap()` via `mem_sbrk()`
5. `place()` the block: split if remainder ≥ 16 bytes (minimum block size)

### 2. `mm_free(bp)` — O(1) Coalescing
1. Mark block as free in header
2. Write footer (only free blocks have footers)
3. Clear `prev_alloc` bit in next block's header
4. **4-case coalescing** using boundary tags:
   - Case 1: Both neighbors allocated → insert to free list
   - Case 2: Next block free → merge with next
   - Case 3: Previous block free → merge with previous
   - Case 4: Both neighbors free → merge all three
5. Insert merged block into appropriate segregated list (LIFO)

### 3. `mm_realloc(ptr, size)` — In-Place Expansion
- If next block is free and combined size is sufficient → **expand in-place** (no `memcpy`)
- Otherwise → `malloc` new block, copy data, free old block
- **Impact**: Eliminates expensive data copying for growing allocations

---

## 🧪 Results

| Metric | Score |
|--------|-------|
| **Total Score** | **94 / 100** |
| **Memory Utilization** | **81.3%** |
| **Throughput** | Top-tier (Segregated list + Best-Fit) |
| **Benchmark Pass** | 11/11 traces (0 segfaults, 0 heap corruption) |

### Key Design Decisions & Trade-offs

| Decision | Benefit | Cost |
|----------|---------|------|
| Footer elimination (allocated blocks) | -4B overhead per block → higher utilization | Slightly more complex header bit management |
| Segregated Free List (16 classes) | Near O(1) allocation for common sizes | 64B heap overhead for list heads |
| Best-Fit (not First-Fit) | Lower fragmentation | Slightly slower search within a class |
| In-place realloc | Avoids memcpy when possible | More complex realloc logic |
| LIFO insertion | O(1) free list insertion | Less temporal locality than address-ordered |

---

## 🔧 Build & Run

**Requirements**: Linux (or WSL), GCC

```bash
# Build
make

# Run all traces
./mdriver

# Run specific trace
./mdriver -f traces/binary-bal.rep

# Verbose mode
./mdriver -V
```

## 📁 Files

```
├── mm.c          # Main allocator implementation (Segregated Free List)
├── mm.h          # Allocator interface
├── memlib.c      # Memory system simulator (mem_sbrk, etc.)
├── memlib.h      # Memory system interface
├── Makefile       # Build configuration
```

## 💡 Key Takeaways

1. **Every byte matters at the system level** — Eliminating the footer on allocated blocks saved 4B per block, directly improving utilization from ~76% to 81.3%
2. **Data structure choice dominates performance** — Segregated Free List reduced allocation search from O(N) to near O(1) compared to Implicit List
3. **Alignment constraints create optimization opportunities** — 8-byte alignment guarantees lower 3 bits are always 0, allowing us to pack `alloc` and `prev_alloc` flags for free
4. **In-place realloc is critical** — Avoiding `memcpy` on growing allocations dramatically improves throughput on realloc-heavy workloads
