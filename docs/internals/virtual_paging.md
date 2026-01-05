# Signal-Based Virtual Paging Protocol

Hyperion implements a user-space paging system ("Virtual Paging") that allows the application to address datasets significantly larger than physical RAM.

## The Virtual Allocation Strategy

On 64-bit systems, the virtual address space is vast (48-bit or 52-bit on ARM64). This allows the runtime to reserve purely virtual memory ranges that do not consume physical RAM until accessed.

### The Protocol

1.  **Reservation**: The runtime maps `1TB` of virtual address space using `mmap` with `PROT_NONE`. This creates an entry in the OS's VMA (Virtual Memory Area) table but commits **zero** physical pages.
2.  **Trap**: A custom signal handler (`sigaction`) is installed for `SIGSEGV` (Linux) and `SIGBUS` (macOS).
3.  **Materialization**: When the CPU executes a load/store instruction on a virtual address in the arena, it triggers a hardware exception. The OS delivers a signal to the process.
4.  **Healing**: The signal handler identifies the faulting page, calls `mprotect(..., PROT_READ | PROT_WRITE)` to execute a "soft page fault," and returns.
5.  **Resume**: The CPU re-executes the instruction. This time, the memory is valid, and execution continues transparently.

## macOS (Apple Silicon) Implementation Details

Achieving this on macOS (ARM64) requires addressing specific kernel behaviors.

### 1. Integer Overflow in Allocation
> [!IMPORTANT]
> **Implementation Note**: Memory calculations larger than 2GB must utilize explicit 64-bit literals.

In C++, `1 << 40` typically overflows a 32-bit signed integer.
**Correction:**
```cpp
// Correct 1TB definition
const size_t ARENA_SIZE = 1ULL << 40; 
```

### 2. Signal Traps: SIGSEGV vs SIGBUS
On Linux (x86_64), accessing `PROT_NONE` memory reliably triggers `SIGSEGV`.
On macOS (Apple Silicon), the Mach microkernel often translates these access violations into `SIGBUS` (Bus Error), specifically when strict protection boundaries are involved.

**Resolution:**
The `MemoryManager` installs handlers for **both** signals.
```cpp
// src/mm/MemoryManager.cpp
if (sigaction(SIGBUS, &sa, nullptr) == -1) //...
if (sigaction(SIGSEGV, &sa, nullptr) == -1) //...
```
The handler inspects `siginfo_t->si_addr` to verify if the fault occurred within the Arena `[BASE, BASE + 1TB)`.

## Initialization Sequence

The sequence of operations during `MemoryManager::initialize()` is strictly ordered:

1.  **Init**: Runtime starts.
2.  **Mmap**: `mmap` reserves the VMA (1TB).
3.  **Handlers**: `sigaction` intercepts crashes.
4.  **Self-Test** (The "Bootstrap" Trap):
    *   The manager deliberately writes to address `BASE + 512GB`.
    *   This triggers the first `SIGBUS`.
    *   The handler catches it, `mprotect`s the page.
    *   The write succeeds.
    *   If the code survives this line, the subsystem is operational.
5.  **Success**: The system enters the main loop.

## Performance Analysis

This mechanism effectively implements a "Software TLB" or user-space page fault handler. While there is overhead (context switch signal delivery), it allows Hyperion to handle datasets limited only by the 48-bit virtual address space, bypassing the OS file cache and swap logic entirely.
