#include "jit/JITOptimizer.hpp"
#include <iostream>
#include <pthread.h>
#include <sys/mman.h>

// External declaration for cache invalidation (standard on Apple Silicon / GCC / Clang)
extern "C" void sys_icache_invalidate(void* start, size_t len);
extern "C" void pthread_jit_write_protect_np(int enabled);

namespace cognitron::core {

JITOptimizer::JITOptimizer() {
    // Constructor logic if needed
}

void JITOptimizer::monitor_branch(void* instruction_addr) {
    // STARTUP COST:
    // Map lookups on hot paths are usually fatal for perf.
    // However, this enters only on cold start or trace compilation phases.
    if (_watched_branches.find(instruction_addr) == _watched_branches.end()) {
        _watched_branches[instruction_addr] = BranchStats{};
    }
}

void JITOptimizer::record_branch_outcome(void* instruction_addr, bool taken) {
    auto it = _watched_branches.find(instruction_addr);
    if (it != _watched_branches.end()) {
        BranchStats& stats = it->second;
        if (stats.is_optimized) return;

        if (taken) stats.taken_count++;
        else stats.not_taken_count++;

        // HEURISTIC TUNING:
        // We set the threshold to 10k iterations.
        // Too low = code churn and I-cache thrashing.
        // Too high = missed optimization opportunities.
        const uint64_t THRESHOLD = 10000;
        
        // POLYMORPHISM LOGIC:
        // "Delete Check" Optimization.
        // If a branch is consistently NOT taken (e.g. error checks, logging configs),
        // we physically overwrite the branch instruction with a NOP.
        // This removes the branch prediction slot consumption entirely.
        
        if (stats.not_taken_count > THRESHOLD && stats.taken_count == 0) {
             // ACTION: Mutate the binary executable code in memory.
             optimize_hot_path(instruction_addr);
             stats.is_optimized = true;
             std::cout << "[SMC] Optimized branch at " << instruction_addr << " (Replaced with NOP)" << std::endl;
        }
    }
}

void JITOptimizer::patch_instruction(void* address, uint32_t new_opcode) {
    // SECURITY CRITICAL: W^X Violation
    // Modern kernels (macOS especially) strictly enforce Write XOR Execute.
    // You cannot write to executable pages by default.
    // We must toggle the JIT write protection bit for this thread.
    
    // 1. UNLOCK: Allow writing to code pages.
    // WARNING: This opens a momentary window for ROP/JIT-spraying attacks.
    pthread_jit_write_protect_np(0);

    // 2. MUTATION: Perform the write.
    // Must be atomic 32-bit store to avoid tearing instructions on ARM64.
    volatile uint32_t* target = static_cast<volatile uint32_t*>(address);
    *target = new_opcode;

    // 3. LOCK: Restore executable protections.
    // Failure to do this results in instant SIGKILL on next instruction fetch.
    pthread_jit_write_protect_np(1);

    // 4. COHERENCY: Flush Instruction Cache.
    // Harvard architectures (ARM64) have separate I-Cache and D-Cache.
    // The CPU pipeline might still hold the stale instruction in the fetch stage.
    // We MUST force a sync/barrier.
    flush_cache(address, sizeof(uint32_t));
}

void JITOptimizer::optimize_hot_path(void* address) {
    // Replace the instruction at `address` with a NOP.
    // ARM64 NOP: 0xD503201F
    // This is the safest mutation as it doesn't change register state.
    patch_instruction(address, ARM64_NOP);
}

void JITOptimizer::flush_cache(void* start, size_t len) {
    // SYSCALL: sys_icache_invalidate
    // Forces the CPU to dump its pipeline and fetch fresh instructions from L2/RAM.
    // Expensive (~100s of cycles), so batch mutations if possible.
    sys_icache_invalidate(start, len);
}

} // namespace cognitron::core
