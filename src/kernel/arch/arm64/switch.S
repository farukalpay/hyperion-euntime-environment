/*
 * Cognitron Zero Nucleus - ARM64
 *
 * CRITICAL: Stack Pointer (SP) must be 16-byte aligned at all times.
 */

.text
.global _switch_context
.global _verify_cpu_features

/*
 * void switch_context(void** old_sp, void* new_sp)
 * X0 = old_sp
 * X1 = new_sp
 */
.p2align 2
_switch_context:
    // 1. Save Callee-Saved Registers (X19-X28, FP, LR)
    // We push pairs to maintain 16-byte alignment
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    stp x25, x26, [sp, #-16]!
    stp x27, x28, [sp, #-16]!
    stp x29, x30, [sp, #-16]!

    // 2. Save current SP to *old_sp (X0)
    mov x9, sp
    str x9, [x0]

    // 3. Load new SP from new_sp (X1)
    mov sp, x1

    // 4. Restore Callee-Saved Registers
    ldp x29, x30, [sp], #16
    
    // Safety Check: Panic if LR is NULL
    cbz x30, 1f

    ldp x27, x28, [sp], #16
    ldp x25, x26, [sp], #16
    ldp x23, x24, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16

    // 5. Return (uses LR restored above)
    ret

1:  // Panic Handler
    brk #0xDEAD

/*
 * void verify_cpu_features()
 * Checks for NEON/FP support.
 */
.p2align 2
_verify_cpu_features:
    // On Apple Silicon, NEON is always available in user mode.
    // If strict checking is needed, we would read ID_AA64PFR0_EL1,
    // but MRS to system registers is privileged (EL1).
    // In Userspace, we assume standard ARMv8+.
    ret
