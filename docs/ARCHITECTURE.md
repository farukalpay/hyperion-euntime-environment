# HYPERION RUNTIME: SYSTEM ARCHITECTURE SPECIFICATION

> **DOCUMENT ID:** HYP-ARCH-001
> **STATUS:** PRODUCTION
> **RELEASE:** 1.0.0

## 1. System Overview

Hyperion is a **Userspace Unikernel Runtime** designed to operate as a high-performance execution environment for semantic data processing. Unlike traditional applications that rely on the Operating System for memory management and scheduling, Hyperion asserts direct control over these resources to ensure deterministic behavior and minimize kernel-mode transitions.

---

## 2. Process Lifecycle

The runtime operates as a closed-loop system, managing data ingestion, processing, and visualization through a cooperative fiber network.

### 2.1 The Signal Path
1.  **Ingestion**: The `InputIngest` fiber periodically polls the system clipboard for new data.
2.  **Scheduling**: The `Scheduler` yields control to the `ProcessingUnit` fiber.
3.  **Compilation**: The `JITOptimizer` generates raw machine code (AVX2/NEON) for the query plan.
4.  **Memory Access**: Execution attempts to access a page within the Virtual Memory arena.
5.  **Exception Handling**: The OS raises a hardware exception (Page Fault). Hyperion's `MemoryManager` intercepts this via `sigaction`, commits the page, and resumes execution.
6.  **Telemetry**: The `SystemMonitor` fiber renders real-time state visualization of the memory map and instructional stream.

---

## 3. Core Subsystems

### 3.1 Cooperative Fiber Scheduler
**Design Philsophy:**
Standard POSIX threads (`pthreads`) incur significant overhead due to kernel involvement in scheduling and state management. Hyperion implements **Fibers** (User-Mode Threads) to reclaim control.

*   **Implementation**: Platform-specific Assembly (`switch.S`) manages the register file (`RBX`/`X19-X30`) and Stack Pointer.
*   **Latency**: Deterministic context switching (< 10ns).

### 3.2 Signal-Based Virtual Paging
**Memory Management Protocol:**

Hyperion reserves a 1 TB Virtual Address Space using `mmap` with `PROT_NONE`. This technique allows the application to address a dataset exceeding physical RAM capacity without explicit file I/O calls.

1.  **Fault**: Accessing `PROT_NONE` memory triggers `SIGBUS` (macOS) or `SIGSEGV` (Linux).
2.  **Trap**: The registered signal handler validates the fault address against the arena bounds.
3.  **Commit**: The handler invokes `mprotect(PROT_READ | PROT_WRITE)` to materialize the specific 4KB page.
4.  **Resume**: Execution continues.

### 3.3 State Visualization
**Telemetry Rendering:**

To provide operational transparency, the runtime includes a `SystemMonitor` that visualizes internal state directly to the TTY.

*   **Heat Decay Map**: Represents memory page residency and access frequency via character density gradients.
*   **Jitter Visualization**: Displays micro-fluctuations in pointer addresses to confirm scheduler liveness during idle states.
*   **Renderer**: Direct ANSI escape sequence generation ensures 60Hz update rates with zero external dependencies.

---

## 4. Module Map

*   `src/kernel/`: Assembly context switchers and Fiber Scheduler.
*   `src/mm/`: Memory Manager and Signal Trap logic.
*   `src/core/`: Processing Unit and JIT Optimization logic.
*   `src/monitor/`: System Monitor (TUI) rendering engine.
