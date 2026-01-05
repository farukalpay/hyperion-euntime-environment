# Memory Model & Allocator Internals

## 1. The "Ghost" Userspace Slab Allocator

Cognitron replaces the standard libc `malloc`/`free` with a custom, high-performance **Slab Allocator** designed specifically for our Unikernel architecture. This allocator lives entirely in user-space and manages the persistent "Ghost Memory" region.

### 1.1 Core Design Principles

1.  **Intrusive Free List (Zero-Overhead)**:
    Instead of maintaining an external `std::map` or `std::vector` of free blocks (which would consume additional memory and cause cache pollution), we store the free list structure *inside the free memory blocks themselves*.
    When a block is free, we cast its `void*` pointer to a `FreeNode` struct containing `next` and `prev` offsets. This means tracking a free block costs exactly **0 bytes** of extra overhead.

2.  **Boundary Tags (Coalescing)**:
    To support O(1) de-fragmentation, every block (used or free) is wrapped with:
    -   **Header**: Size & State (Used/Free).
    -   **Footer**: A copy of the Header.
    
    This allows the `Free()` operation to check the *Footer* of the *previous* block in memory to immediately determine if it can merge left, without traversing the list.

3.  **Strict 64-Byte Alignment**:
    All allocations are aligned to 64-byte boundaries.
    -   **Why?**
        -   **AVX2/NEON SIMD**: Vector instructions require aligned memory for maximum throughput.
        -   **Cache Lines**: Modern CPUs have 64-byte cache lines. Aligning blocks prevents "False Sharing" (where two threads fight over the same cache line because their data happens to sit next to each other).

4.  **Spinlocks (Concurrency)**:
    We use a custom `Spinlock` based on `std::atomic_flag`.
    -   **Why?** In a Unikernel/Fiber environment, we want to avoid the heavy context switch of an OS-level `std::mutex`. If a lock is held for only a few nanoseconds (which is true for allocator ops), spinning is far cheaper than sleeping.

### 1.2 Memory Layout Diagram

```text
    Base Address (64-byte aligned)
    +-----------------------------------------------------------------------+
    | Global Allocator Header (Control Struct)                              |
    +-----------------------------------------------------------------------+
    |                                                                       |
    | [ Block Header (64 bytes) ]                                           |
    |   - Size (63 bits) | FreeBit (1 bit)                                  |
    |   - Padding to 64 bytes (Avoids False Sharing)                        |
    |                                                                       |
    +-----------------------------------------------------------------------+ <--- User Pointer
    |                                                                       |
    | USER PAYLOAD / FREE NODE DATA                                         |
    |                                                                       |
    | IF FREE:                                                              |
    |   [ Next Free Offset (8 bytes) ]                                      |
    |   [ Prev Free Offset (8 bytes) ]                                      |
    |   ... Unused ...                                                      |
    |                                                                       |
    | IF USED:                                                              |
    |   ... User Data ...                                                   |
    |                                                                       |
    +-----------------------------------------------------------------------+
    | [ Block Footer (8 bytes) ]                                            |
    |   - Copy of Size | FreeBit for Coalescing Left                        |
    +-----------------------------------------------------------------------+
    | [ Next Block Header ... ]                                             |
```

## 2. Algorithms

### Allocation (First-Fit)
1.  Iterate the Intrusive Free List.
2.  Find the first block where `BlockSize >= RequestSize + Overhead`.
3.  **Split**: If the block is significantly larger than requested, split it into [Used Part] and [Remaining Free Part].
4.  Return the pointer to the Payload.

### De-Allocation (Coalescing)
1.  Mark current block as **Free**.
2.  **Coalesce Right**: Look at `Current + Size`. If the next block's header says "Free", merge them.
3.  **Coalesce Left**: Look at `Current - sizeof(Footer)`. Read the Previous Block's Size. Jump back to its Header. If it says "Free", merge them.
4.  Insert into Free List (if not merged).

## 3. ABI Constraints
-   **Minimum Block Size**: 128 Bytes (Header + pointers + Footer).
-   **Max Single Allocation**: Defined by `SlabAllocator::total_size`.
