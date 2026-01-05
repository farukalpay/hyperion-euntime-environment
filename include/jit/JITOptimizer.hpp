#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace cognitron::core {

/**
 * @brief Handles Self-Modifying Code (SMC) operations.
 * 
 * Monitors execution hotspots and rewrites binary instructions at runtime
 * to optimize performance (e.g., replacing conditional branches with NOPs).
 */
class JITOptimizer {
public:
    static constexpr uint32_t ARM64_NOP = 0xD503201F;

    JITOptimizer();
    
    /**
     * @brief Registers a memory address to be monitored for branch prediction optimization.
     * @param instruction_addr Address of the conditional branch (B.NE/B.EQ).
     */
    void monitor_branch(void* instruction_addr);
    
    /**
     * @brief Records a "taken" or "not taken" event for a monitored branch.
     * @param instruction_addr The address being monitored.
     * @param taken True if the branch was taken.
     */
    void record_branch_outcome(void* instruction_addr, bool taken);

    /**
     * @brief Modifies the instruction at the given address.
     * Dangerous! Requires W^X handling.
     * @param address Address to patch.
     * @param new_opcode New 32-bit ARM64 opcode.
     */
    void patch_instruction(void* address, uint32_t new_opcode);

    /**
     * @brief Optimizes a specific hot path by identifying the branch and NOPing it.
     * @param address Address of the branch instruction.
     */
    void optimize_hot_path(void* address);

private:
   struct BranchStats {
       uint64_t taken_count = 0;
       uint64_t not_taken_count = 0;
       bool is_optimized = false; // True if we already patched it
   };
   
   std::unordered_map<void*, BranchStats> _watched_branches;

   // Helper to invalidate instruction cache after patching
   void flush_cache(void* start, size_t len);
};

} // namespace cognitron::core
