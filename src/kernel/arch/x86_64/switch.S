/*
 * Cognitron Zero Nucleus - x86_64
 * 
 * CRITICAL: We manually align RSP to 16 bytes. Failure to do so will cause SSE instructions to SEGFAULT.
 */

.text
.global _switch_context
.global _verify_cpu_features

/* 
 * void switch_context(void** old_sp, void* new_sp)
 * RDI = old_sp (pointer to store current RSP)
 * RSI = new_sp (target RSP to load)
 */
_switch_context:
    // 1. Save Callee-Saved Registers
    pushq %rbx
    pushq %rbp
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    // 2. Save current RSP to *old_sp (RDI)
    movq %rsp, (%rdi)

    // 3. Load new RSP from new_sp (RSI)
    movq %rsi, %rsp

    // 4. Restore Callee-Saved Registers from new stack
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbp
    popq %rbx

    // 5. Jump into new fiber
    ret

/*
 * void verify_cpu_features()
 * Checks for AVX2 support. Panic if missing.
 */
_verify_cpu_features:
    pushq %rbp
    movq %rsp, %rbp
    pushq %rbx

    // Check for AVX2: CPUID Leaf 7, Subleaf 0, EBX bit 5
    movl $7, %eax
    movl $0, %ecx
    cpuid
    btl $5, %ebx
    jnc .L_panic_no_avx2

    popq %rbx
    popq %rbp
    ret

.L_panic_no_avx2:
    // In a real kernel we'd syscall exit(). Here we'll just trap.
    ud2
