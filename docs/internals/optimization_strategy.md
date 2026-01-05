# JIT Compilation & Dynamic Optimization Strategy

## 1. Introduction

Hyperion utilizes a **JIT Optimizer** to modify its own binary code at runtime. This capability allows the system to specialize the instruction stream based on real-time data distribution, effectively compiling the query plan into machine code.

## 2. The Application Binary Interface (ABI)

Hyperion targets the **ARM64 (AAPCS64)** and **x86-64 (System V AMD64)** calling conventions.

### 2.1 Register Discipline

When the JIT constructs a function, it MUST respect the volatile/non-volatile register split.

**ARM64 Rules:**
- `X0` - `X7`: Argument / Result registers (Caller Saved).
- `X19` - `X28`: Callee Saved (Must preserve).
- `X29` (FP), `X30` (LR): Formatting frame pointers is manually enforced.

### 2.2 Register Spilling

When the JIT runs out of physical registers (e.g., heavily unrolled SIMD loops), registers are spilled to the stack.
The stack frame layout is rigid:

```
[ SP + 0x00 ] -> Saved FP (X29)
[ SP + 0x08 ] -> Saved LR (X30)
[ SP + 0x10 ] -> Spilled Register X19
[ SP + 0x18 ] -> ...
```

**Critical Warning:** The stack MUST be 16-byte aligned. Failure to align `SP` before a `BL` (Branch Link) instruction causes hardware faults on Apple Silicon.

## 3. Dynamic Mutation

Traditional JITs compile once. Hyperion **re-compiles live**.

### 3.1 The "branch-to-NOP" Algorithm

Many branches exist solely for error checking or edge cases that never occur in a sanitized dataset.

**Scenario:**
```asm
LDR  w0, [x1]      ; Load data
CBZ  w0, _error    ; Branch if Zero (Check null)
ADD  w0, w0, #1    ; Payload
```

If the `CBZ` is *never* taken after a training period, the JIT Optimizer identifies it as overhead.

**Mutation:**
The `CBZ` is overwritten with `NOP`.

```asm
LDR  w0, [x1]
NOP                ; 0xD503201F (Executes in 0 cycles)
ADD  w0, w0, #1
```

### 3.2 W^X Constraints

Operating Systems enforce **W^X** (Write XOR Execute). A page cannot be writable and executable simultaneously.

**The Patching Protocol:**
1. `pthread_jit_write_protect_np(0)` -> Page becomes RW- (Not Executable).
2. **Write**: The instruction bytes are patched.
3. `pthread_jit_write_protect_np(1)` -> Page becomes R-X (Executable).
4. **I-Cache Flush**: `sys_icache_invalidate`. (Essential ensures the CPU pipeline discards the stale instruction).

## 4. Security Implications

This engine introduces dynamic code generation capabilities inside the process address space.
- To mitigate ROP (Return Oriented Programming) attacks, the JIT pages are placed at randomized offsets (ASLR).
- All generated code is verified against a "Safe Opcode Filter" before emission.
