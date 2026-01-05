# ABI Contracts & Register Discipline

## The 64-bit Contract

Hyperion operates as a guest within the host OS (macOS/Linux) but enforces its own strict calling conventions for JIT-compiled code. The runtime adheres to **System V AMD64** and **ARM64 AAPCS**.

### 1. General Purpose Registers (GPR)

When switching context between the Host (C++ Processing Unit) and Guest (JIT Code), specific registers must be preserved to maintain stability.

#### AMD64 (x86_64) Specification

| Register | Role | Responsibility |
| :--- | :--- | :--- |
| **RBX** | Callee-Saved | **MUST SAVE/RESTORE**. Used as Base Pointer for the Virtual Arena. |
| **RSP** | Stack Pointer | **MUST PRESERVE**. 16-byte alignment mandatory. |
| **RBP** | Frame Pointer | **MUST PRESERVE**. Required for stack unwinding and debugging. |
| **R12-R15**| Callee-Saved | **MUST SAVE/RESTORE**. High-performance scratchpad. |
| **RAX** | Return Value | **VOLATILE**. |
| **RCX/RDX**| Arguments | **VOLATILE**. |
| **RDI/RSI**| Arguments | **VOLATILE**. |

#### ARM64 (Apple Silicon) Specification

| Register | Role | Responsibility |
| :--- | :--- | :--- |
| **X19-X28**| Callee-Saved | **MUST SAVE/RESTORE**. |
| **X29 (FP)**| Frame Pointer| **MUST LINK**. |
| **X30 (LR)**| Link Register| **MUST SAVE**. Contains return address. |
| **SP** | Stack Pointer| **MUST ALIGN**. 16-byte alignment fault if violated. |

### 2. The Vector Registers (SIMD)

The JIT Optimizer utilizes AVX2 (YMM) and NEON (Q-Regs) for high-throughput data processing.

*   **x86_64**: `XMM0`-`XMM15` are **Volatile**. The host C++ compiler assumes destruction. This enables the JIT to maximize register utilization without save/restore overhead.
*   **ARM64**: `V0`-`V31`. Lower 64-bits of `V8`-`V15` are technically callee-saved, but in "Unikernel" mode, we treat them as volatile for performance, provided the host function does not rely on floating-point math across the call boundary.

### 3. Stack Frame Layout

When the JIT spills registers, it creates a rigid stack frame:

```text
[ High Address ]
+------------------+
|   Return Addr    |  (8 bytes)
+------------------+
|   Saved RBP      |  <- RBP points here
+------------------+
|   Saved RBX      |
+------------------+
|   Saved R12      |
+------------------+
|   ...            |
+------------------+
|   LocalVars      |
+------------------+
[ Low Address  ]  <- RSP
```
