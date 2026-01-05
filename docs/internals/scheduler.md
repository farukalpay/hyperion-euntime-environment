# Userspace Scheduler Architecture

## Rationale: OS Bypass

Operating System schedulers (CFS on Linux, XNU on macOS) are general-purpose, optimizing for fairness and responsiveness across hundreds of processes.

Hyperion is a **Unikernel Runtime**. Priorities include:
1.  **Cache Locality**: Keeping related data in L1/L2 cache.
2.  **Deterministic Latency**: Eliminating random preemption.
3.  **Context Switch Speed**: Switching OS threads costs ~1-2us. Switching Fibers costs ~10ns.

## Architecture: Cooperative Multitasking

Hyperion implements a **Fiber** system (User-Mode Threads).
CPU cores run one "Host Thread" each. Inside that thread, the runtime manually switches the stack pointer (`RSP`/`SP`) to jump between tasks.

### The Context Switch (`switch_context.S`)

The transition occurs in Assembly. A "Task" is defined as a saved Register State + a Stack.

To switch tasks:
1.  Push all Callee-Saved registers (RBX, RBP, R12-R15) onto current stack.
2.  Save current `RSP` into the Old Task struct.
3.  Load new `RSP` from the New Task struct.
4.  Pop all registers.
5.  `RET` (Returns into the new task's instruction stream).

### The Scheduler Loop

```cpp
void Scheduler::Run() {
    while (true) {
        Fiber* next = PickNextFiber();
        
        if (next == nullptr) {
             // Idle: Use MONITOR/MWAIT to sleep efficiently
             CpuRelax(); 
        } else {
             SwitchTo(next);
        }
    }
}
```

## Stack Management

Each Fiber is allocated a fixed 1MB stack (configurable).
**Stack Canaries** (magic values) are placed at the end of the stack.
If the canary is corrupted, the Scheduler detects the overflow and terminates the task before heap corruption occurs.
