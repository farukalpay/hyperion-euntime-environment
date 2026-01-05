#pragma once

#include <cstdint>
#include <atomic>
#include <cstddef>
#include <new> // For std::launder if needed, or placement new

namespace Cognitron::Core {

    /**
     *  MEMORY LAYOUT DIAGRAM
     *  =====================
     *  
     *  Base Address (64-byte aligned)
     *  +-----------------------------------------------------------------------+
     *  | Global Allocator Header (Control Struct)                              |
     *  +-----------------------------------------------------------------------+
     *  |                                                                       |
     *  | [ Block Header (64 bytes) ]                                           |
     *  |   - Size (63 bits) | FreeBit (1 bit)                                  |
     *  |   - Padding to 64 bytes (Avoids False Sharing)                        |
     *  |                                                                       |
     *  +-----------------------------------------------------------------------+ <--- User Pointer (Aligned 64)
     *  |                                                                       |
     *  | USER PAYLOAD / FREE NODE DATA                                         |
     *  |                                                                       |
     *  | IF FREE:                                                              |
     *  |   [ Next Free Offset (8 bytes) ]                                      |
     *  |   [ Prev Free Offset (8 bytes) ]                                      |
     *  |   ... Unused ...                                                      |
     *  |                                                                       |
     *  | IF USED:                                                              |
     *  |   ... User Data ...                                                   |
     *  |                                                                       |
     *  +-----------------------------------------------------------------------+
     *  | [ Block Footer (8 bytes) ]                                            |
     *  |   - Copy of Size | FreeBit for Coalescing Left                        |
     *  +-----------------------------------------------------------------------+
     *  | [ Next Block Header ... ]                                             |
     *
     *  WHY 64-BYTE ALIGNMENT?
     *  1. AVX2/AVX-512 and NEON SIMD instructions operate fastest (or only) 
     *     on aligned memory. Unaligned accesses cause penalties or faults.
     *  2. Cache Line alignment (64 bytes) prevents "False Sharing" where
     *     updates to a header by one core invalidate the cache line for a 
     *     neighboring block processed by another core.
     */

    // --- High-Performance Spinlock ---
    class Spinlock {
    public:
        void lock() noexcept {
            while (flag.test_and_set(std::memory_order_acquire)) {
                // Hint to CPU that we are spinning (pause instruction)
                // x86: _mm_pause(); ARM: __yield();
                // keeping it portable C++20/standard dependent or simple for now
#if defined(__x86_64__) || defined(_M_X64)
                __builtin_ia32_pause();
#elif defined(__aarch64__)
                __builtin_arm_yield();
#endif
            }
        }

        void unlock() noexcept {
            flag.clear(std::memory_order_release);
        }

    private:
        std::atomic_flag flag = ATOMIC_FLAG_INIT;
    };

    // RAII Wrapper for Spinlock
    class SpinLockGuard {
    public:
        explicit SpinLockGuard(Spinlock& lock) : m_lock(lock) { m_lock.lock(); }
        ~SpinLockGuard() { m_lock.unlock(); }
    private:
        Spinlock& m_lock;
    };

    // --- Block Headers & Footers ---

    constexpr size_t ALIGNMENT = 64;
    constexpr size_t MIN_BLOCK_SIZE = 128; // Header(64) + Payload(min 8) + Footer(8) -> Round up

    // Aligned Header to ensure Payload starts at 64-byte boundary and avoid false sharing
    struct alignas(ALIGNMENT) BlockHeader {
        uint64_t size_and_state; // [63 bits: Size][1 bit: IsFree]

        static constexpr uint64_t FREE_MASK = 1ULL;
        static constexpr uint64_t SIZE_MASK = ~FREE_MASK;

        uint64_t GetSize() const { return size_and_state & SIZE_MASK; }
        bool IsFree() const { return (size_and_state & FREE_MASK) != 0; }
        
        void Set(uint64_t size, bool free) {
            size_and_state = (size & SIZE_MASK) | (free ? FREE_MASK : 0);
        }
        
        void SetFree(bool free) {
            if (free) size_and_state |= FREE_MASK;
            else size_and_state &= SIZE_MASK;
        }

        void SetSize(uint64_t size) {
            bool free = IsFree();
            size_and_state = (size & SIZE_MASK) | (free ? FREE_MASK : 0);
        }
    };

    // Compact Footer for coalescing (placed at end of block)
    struct BlockFooter {
        uint64_t size_and_state;
    };

    // Intrusive Free List Node (Lives inside the FREE payload)
    struct FreeNode {
        uint64_t next_offset; // Relative offset from Base
        uint64_t prev_offset; // Relative offset from Base
    };

    // --- Slab Allocator ---

    class SlabAllocator {
    public:
        SlabAllocator(char* base_addr, size_t total_size, uint64_t start_offset) 
            : m_base(base_addr), 
              m_total_size(total_size), 
              m_base_offset(start_offset),
              m_free_list_head_offset(0) // 0 implies null in our offset-based system
        { 
            Init();
        }

        // Initialize the memory region as one giant free block
        void Init() {
            SpinLockGuard guard(m_lock);

            // Align start to 64 bytes
            uintptr_t base = reinterpret_cast<uintptr_t>(m_base);
            uintptr_t aligned_base = (base + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
            size_t adjustment = aligned_base - base;
            
            // Adjust offsets
            uint64_t effective_start = m_base_offset + adjustment;
            uint64_t effective_size = m_total_size - adjustment;

            // Ensure we have space for at least one block
            if (effective_size < sizeof(BlockHeader) + sizeof(BlockFooter)) {
                return; // Too small
            }

            // Create Initial Giant Free Block
            // Start of Heap relative to Base
            m_first_block_offset = effective_start; 

            // Calculate Usable Size
            // Block = Header + [Payload + Footer]
            // We want the Payload to be aligned??
            // Actually, if Header is 64-aligned, Payload is 64-aligned.
            // effective_start is 64-aligned relative to 0 if m_base is aligned?
            // We assume m_base + m_first_block_offset is 64-byte aligned.

            // Setup Header
            BlockHeader* header = GetPtr<BlockHeader>(m_first_block_offset);
            uint64_t usable_size = effective_size - sizeof(BlockHeader) - sizeof(BlockFooter);
            
            // Align size down to maintain end alignment
            usable_size &= ~(ALIGNMENT - 1); // Not strictly necessary for size, but good for cleanliness
            
            header->Set(effective_size, true); // Total size including header/footer

            // Setup Footer
            BlockFooter* footer = GetFooter(header);
            footer->size_and_state = header->size_and_state;

            // Setup Free Node
            FreeNode* node = GetPayload<FreeNode>(header);
            node->next_offset = 0;
            node->prev_offset = 0;

            m_free_list_head_offset = m_first_block_offset;
        }

        uint64_t Allocate(size_t size) {
            if (size == 0) return 0;

            // 1. Enforce Alignment on Size
            // We need enough space for Payload + Footer, aligned to 64
            size_t footer_size = sizeof(BlockFooter);
            size_t payload_size = (size + 63) & ~63; // User aligned size
            
            // Total block size needed = Header + Payload + Footer
            // But we actually split blocks.
            // Minimum size logic?
            size_t required_total_size = sizeof(BlockHeader) + payload_size + footer_size;
            // Round up total size to alignment to keep next block headers aligned
            required_total_size = (required_total_size + ALIGNMENT - 1) & ~(ALIGNMENT - 1);

            SpinLockGuard guard(m_lock);

            // 2. First-Fit Search
            uint64_t curr_offset = m_free_list_head_offset;
            while (curr_offset != 0) {
                BlockHeader* header = GetPtr<BlockHeader>(curr_offset);
                FreeNode* free_node = GetPayload<FreeNode>(header);

                if (header->GetSize() >= required_total_size) {
                    // FOUND
                    uint64_t remaining_size = header->GetSize() - required_total_size;

                    // 3. Split if enough space remains
                    // We need enough for a new Header + Min Payload + Footer
                    if (remaining_size >= sizeof(BlockHeader) + 64 + sizeof(BlockFooter)) {
                        
                        // Shrink current block
                        header->Set(required_total_size, false);
                        UpdateFooter(header);

                        // Create new Free Block
                        uint64_t new_block_offset = curr_offset + required_total_size;
                        BlockHeader* new_header = GetPtr<BlockHeader>(new_block_offset);
                        new_header->Set(remaining_size, true);
                        UpdateFooter(new_header);

                        // Insert New Block into Free List in place of current
                        FreeNode* new_node = GetPayload<FreeNode>(new_header);
                        new_node->prev_offset = free_node->prev_offset;
                        new_node->next_offset = free_node->next_offset;

                        // Fix Links
                        if (new_node->prev_offset != 0) {
                            BlockHeader* prev = GetPtr<BlockHeader>(new_node->prev_offset);
                            GetPayload<FreeNode>(prev)->next_offset = new_block_offset;
                        } else {
                            m_free_list_head_offset = new_block_offset;
                        }

                        if (new_node->next_offset != 0) {
                            BlockHeader* next = GetPtr<BlockHeader>(new_node->next_offset);
                            GetPayload<FreeNode>(next)->prev_offset = new_block_offset;
                        }

                    } else {
                        // Use whole block
                        header->SetUsed(false); // Used
                        // Remove from free list
                        if (free_node->prev_offset != 0) {
                            BlockHeader* prev = GetPtr<BlockHeader>(free_node->prev_offset);
                            GetPayload<FreeNode>(prev)->next_offset = free_node->next_offset;
                        } else {
                            m_free_list_head_offset = free_node->next_offset;
                        }

                        if (free_node->next_offset != 0) {
                            BlockHeader* next = GetPtr<BlockHeader>(free_node->next_offset);
                            GetPayload<FreeNode>(next)->prev_offset = free_node->prev_offset;
                        }
                        
                        // Mark Header/Footer as Used
                        header->SetFree(false);
                        UpdateFooter(header);
                    }

                    // Return Payload Offset
                    return curr_offset + sizeof(BlockHeader);
                }

                curr_offset = free_node->next_offset;
            }

            return 0; // OOM
        }

        void Free(uint64_t payload_offset, size_t size_hint = 0) {
            if (payload_offset == 0) return;

            SpinLockGuard guard(m_lock);

            // Calculate Header Offset
            uint64_t block_offset = payload_offset - sizeof(BlockHeader);
            BlockHeader* header = GetPtr<BlockHeader>(block_offset);

            // Double Free check?
            if (header->IsFree()) return; 

            // Mark as Free
            header->SetFree(true);
            UpdateFooter(header);

            // COALESCE RIGHT
            // Check if next block exists and is free
            uint64_t next_block_offset = block_offset + header->GetSize();
            if (next_block_offset < m_total_size + m_base_offset) { // Bounds check relies on accurate sizes
                 // Wait, we need to be careful about strict bounds
                 // Let's check pointer validity
                 BlockHeader* next_hdr = GetPtr<BlockHeader>(next_block_offset);
                 if (next_hdr && next_hdr->IsFree()) {
                     // Merge!
                     RemoveFromFreeList(next_block_offset);
                     
                     uint64_t new_size = header->GetSize() + next_hdr->GetSize();
                     header->SetSize(new_size);
                     UpdateFooter(header);
                 }
            }

            // COALESCE LEFT
            // Check prev block using footer
            // We need to know where the prev footer IS.
            // It is at block_offset - sizeof(BlockFooter)
            if (block_offset > m_first_block_offset) {
                BlockFooter* prev_footer = reinterpret_cast<BlockFooter*>(
                    reinterpret_cast<char*>(header) - sizeof(BlockFooter)
                );
                
                // Decode footer
                uint64_t prev_size = prev_footer->size_and_state & BlockHeader::SIZE_MASK;
                bool prev_free = (prev_footer->size_and_state & BlockHeader::FREE_MASK) != 0;

                if (prev_free) {
                    uint64_t prev_offset = block_offset - prev_size;
                    BlockHeader* prev_hdr = GetPtr<BlockHeader>(prev_offset);
                    
                    // Merge current into prev
                    // Previous is already in free list? Yes.
                    // We just extend it.
                    // BUT we still need to add THIS block to free list if we didn't merge left?
                    // Logic:
                    // 1. If merge left: Prev becomes the "current" big block. We don't add "header" to free list, Prev is already there. we just grow it.
                    // 2. If NO merge left: We add "header" to free list.
                    
                    uint64_t new_size = prev_hdr->GetSize() + header->GetSize();
                    prev_hdr->SetSize(new_size);
                    UpdateFooter(prev_hdr);
                    
                    // We are done. The 'Prev' block is already in the free list.
                    return; 
                }
            }

            // If we didn't Coalesce Left, we must add 'header' to the free list
            InsertHead(block_offset);
        }
        
    private:
        char* m_base;
        size_t m_total_size;
        uint64_t m_base_offset; // Virtual offset where heap starts
        
        uint64_t m_free_list_head_offset;
        uint64_t m_first_block_offset;

        Spinlock m_lock;

        template<typename T>
        T* GetPtr(uint64_t offset) {
            return reinterpret_cast<T*>(m_base + (offset - m_base_offset));
        }

        template<typename T>
        T* GetPayload(BlockHeader* header) {
            return reinterpret_cast<T*>(reinterpret_cast<char*>(header) + sizeof(BlockHeader));
        }

        BlockFooter* GetFooter(BlockHeader* header) {
            return reinterpret_cast<BlockFooter*>(
                reinterpret_cast<char*>(header) + header->GetSize() - sizeof(BlockFooter)
            );
        }

        void UpdateFooter(BlockHeader* header) {
            BlockFooter* footer = GetFooter(header);
            footer->size_and_state = header->size_and_state;
        }

        void InsertHead(uint64_t offset) {
            BlockHeader* header = GetPtr<BlockHeader>(offset);
            FreeNode* node = GetPayload<FreeNode>(header);
            
            node->next_offset = m_free_list_head_offset;
            node->prev_offset = 0;

            if (m_free_list_head_offset != 0) {
                 BlockHeader* old_head = GetPtr<BlockHeader>(m_free_list_head_offset);
                 GetPayload<FreeNode>(old_head)->prev_offset = offset;
            }
            m_free_list_head_offset = offset;
        }

        void RemoveFromFreeList(uint64_t offset) {
           BlockHeader* header = GetPtr<BlockHeader>(offset);
           FreeNode* node = GetPayload<FreeNode>(header);

           if (node->prev_offset != 0) {
               BlockHeader* prev = GetPtr<BlockHeader>(node->prev_offset);
               GetPayload<FreeNode>(prev)->next_offset = node->next_offset;
           } else {
               m_free_list_head_offset = node->next_offset;
           }

           if (node->next_offset != 0) {
               BlockHeader* next = GetPtr<BlockHeader>(node->next_offset);
               GetPayload<FreeNode>(next)->prev_offset = node->prev_offset;
           }
        }
    };

} // namespace Cognitron::Core
