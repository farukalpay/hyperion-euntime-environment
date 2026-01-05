# Hyperion Runtime Environment
### High-Performance Signals-Based Userspace Unikernel

<p align="center">
  <img src="https://github.com/farukalpay/hyperion-euntime-environment/blob/main/img1.png?raw=true" width="100%">
</p>

The **Hyperion Runtime** is a specialized execution environment designed for high-throughput semantic processing. It implements a **Userspace Unikernel** architecture, bypassing standard Operating System abstractions to achieve deterministic latency and direct hardware control. By leveraging cooperative fiber scheduling and a signal-based virtual paging mechanism, Hyperion minimizes context switch overhead and maximizes data locality.

## 1. Core Architecture

The system functions as a monolithic process operating in a single address space, managing its own virtual memory and thread scheduling.

```mermaid
graph TD
    A[Input Ingest] -->|Raw Stream| B(Processing Unit);
    B -->|Tokenize & Vectorize| C{JIT Optimizer};
    C -->|Direct Memory Write| D[Virtual Memory (1TB)];
    D -.->|SIGBUS/SIGSEGV| E[Memory Manager Trap];
    E -.->|mprotect| F[Physical RAM];
    
    subgraph KERNEL
    S[Scheduler] -->|Context Switch| T1[Fiber: Monitor];
    S -->|Context Switch| T2[Fiber: Analysis];
    T1 -->|Yield| S
    end
```

## 2. Technical Components

### 2.1 The Kernel (Scheduler)
Hyperion implements a cooperative scheduler managing lightweight **Fibers** (User-Mode Threads).
*   **Mechanism**: Raw Assembly context switching (`switch.S`) preserving callee-saved registers.
*   **Performance**: Context switch latency < 10 nanoseconds.
*   **Policy**: Round-Robin with explicit yield points.

### 2.2 Memory Manager (Virtual Paging)
Instead of relying on the OS file cache, Hyperion manages a 1 TB virtual address space (`0x100000000000` base).
*   **Lazy Allocation**: Pages are reserved via `mmap(PROT_NONE)`.
*   **Signal Handling**: Access violations trigger `SIGBUS` (macOS) or `SIGSEGV` (Linux).
*   **Resolution**: The `MemoryManager` intercepts the signal, commits physical pages via `mprotect`, and resumes execution transparently.
*   **Resolution**: The `MemoryManager` intercepts the signal, commits physical pages via `mprotect`, and resumes execution transparently.

### 2.3 Intrusive Slab Allocator
A custom "Linux-style" allocator handling the persistent Ghost Memory region.
*   **Zero-Overhead Tracking**: Free list nodes are storing *inside* the free blocks (intrusive).
*   **De-Fragmentation**: O(1) Coalescing using Boundary Tags (Footers) to merge adjacent blocks.
*   **Concurrency**: Nanosecond-level `Spinlock` using `std::atomic_flag` (no mutex overhead).
*   **Alignment**: Strict 64-byte alignment for AVX2/NEON SIMD compatibility.
### 2.3 Processing Unit
The central logic core (formerly Engine) handling data ingestion and transformations.
*   **Concurrency**: Single-Producer Single-Consumer (SPSC) Lock-Free Ring Buffer.
*   **Vectorization**: AVX2/NEON intrinsics for mathematical operations.

### 2.4 System Monitor
A zero-dependency terminal interface for real-time state visualization.
*   **Telemetry**: Visualizes stack depth, memory residency (heat decay), and CPU opcode streams.
*   **Rendering**: Double-buffered ANSI sequence emitter.

## 3. Directory Structure

```text
hyperion/
├── src/
│   ├── main.cpp                # Bootstrap & Signal Registration
│   ├── kernel/                 # Scheduler & Context Switching
│   ├── mm/                     # Memory Manager (Signal Traps)
│   ├── core/                   # Processing Unit & Logic
│   ├── jit/                    # JIT Optimizer & Binary Patching
│   └── monitor/                # System Monitor (TUI)
├── include/                    # Header specifications
└── docs/                       # Technical Documentation
    ├── ARCHITECTURE.md         # System Design Document
    └── internals/
        ├── virtual_paging.md   # Memory Protocol Spec
        ├── optimization_strategy.md 
        ├── scheduler.md        # Fiber Implementation
        └── abi_contracts.md    # Register Discipline
```

## 4. Compilation & Deployment

**System Requirements**: 
- **Architecture**: ARM64 (Apple Silicon) or x86_64.
- **Compiler**: C++23 compliant (Clang 15+ / GCC 12+).

```bash
# Clean Build
make clean && make

# Execution
./hyperion
```

## 5. Troubleshooting (macOS)

**Code Signing & JIT Entitlements**
The JIT Optimizer requires specific hardened runtime entitlements on macOS (`com.apple.security.cs.allow-jit`). The `Makefile` attempts to apply these automatically. If execution fails with `Killed: 9`, ensure the entitlements are valid.

---
*Hyperion Runtime Systems Division.*
