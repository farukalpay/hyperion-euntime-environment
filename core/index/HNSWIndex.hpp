#pragma once

#include <vector>
#include <random>
#include <cmath>
#include <memory/SlabAllocator.hpp>
#include "../math/SIMD.hpp"

namespace Cognitron::Core {

    // --- HNSW Graph Node ---
    // Stored in Slab. Links are OFFSETS.
    
    struct HNSWNode {
        uint32_t id;         // DocID
        uint64_t vector_offset; // Offset to the actual vector data
        
        // Simplicity: Fixed max neighbors for level 0. 
        // In real HNSW, this is dynamic per level.
        // Let's implement a single-layer NSW for simplicity of the prompt "Simplistic Graph-Based Index".
        // Or essentially a graph where each node has k neighbors.
        
        uint32_t neighbor_count;
        uint64_t neighbors[16]; // Offsets to other HNSWNode structs
    };

    class HNSWIndex {
    public:
        HNSWIndex(SlabAllocator& allocator) : m_allocator(allocator) {
            // Need to store EntryPoint offset in a header known location
        }

        // Add node to graph
        void Insert(uint32_t id, uint64_t vector_offset, const std::vector<float>& vec_data) {
            // 1. Allocate Node
            uint64_t node_offset = m_allocator.Allocate(sizeof(HNSWNode));
            if (!node_offset) return;

            HNSWNode* node = m_allocator.GetPtr<HNSWNode>(node_offset);
            node->id = id;
            node->vector_offset = vector_offset;
            node->neighbor_count = 0;

            // 2. Greedy Search for nearest neighbors (Simulated)
            // In a real HNSW, we traverse from entry point.
            // Here, for the "Infrastructure" tasks, we demonstrate layout control.
            
            // Connect to Entry Point if exists
            if (m_entry_point_offset != 0) {
                // Bidirectional link (simplified)
                HNSWNode* entry = m_allocator.GetPtr<HNSWNode>(m_entry_point_offset);
                if (entry->neighbor_count < 16) {
                    entry->neighbors[entry->neighbor_count++] = node_offset;
                }
                if (node->neighbor_count < 16) {
                    node->neighbors[node->neighbor_count++] = m_entry_point_offset;
                }
            } else {
                m_entry_point_offset = node_offset;
            }
        }
        
        uint64_t GetEntryPoint() const { return m_entry_point_offset; }
        void SetEntryPoint(uint64_t offset) { m_entry_point_offset = offset; }

    private:
        SlabAllocator& m_allocator;
        uint64_t m_entry_point_offset = 0;
    };

} // namespace Cognitron::Core
