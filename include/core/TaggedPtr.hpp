#pragma once

#include <cstdint>
#include <iostream>

namespace Hyperion::Core {

    /**
     * @brief TaggedPtr utilizes the top 16 unused bits of a 64-bit pointer.
     * 
     * Architecture Config:
     * - Address Space: 48 bits (User Space usually)
     * - Tag Space: Top 16 bits
     * 
     * Layout:
     * [63 ... 48] [47 ... 0]
     *   Metadata    Address
     * 
     * Metadata Layout (16 bits):
     * [63]: Dirty Bit (1)
     * [62-60]: Quantization Level (3)
     * [59-48]: Access Count / LRU (12)
     */
    template <typename T>
    class TaggedPtr {
    public:
        // Masks
        static constexpr uintptr_t ADDR_MASK = 0x0000FFFFFFFFFFFFULL;
        static constexpr uintptr_t TAG_MASK  = 0xFFFF000000000000ULL;
        
        static constexpr uintptr_t DIRTY_BIT_MASK = 1ULL << 63;
        static constexpr uintptr_t QUANT_SHIFT = 60;
        static constexpr uintptr_t QUANT_MASK = 0x7ULL << QUANT_SHIFT; // 3 bits
        static constexpr uintptr_t ACCESS_SHIFT = 48;
        static constexpr uintptr_t ACCESS_MASK = 0xFFFULL << ACCESS_SHIFT; // 12 bits

        TaggedPtr(T* ptr = nullptr) {
            uintptr_t raw = reinterpret_cast<uintptr_t>(ptr);
            // Ensure incoming pointer is clean (in case it came from another tagged ptr source improperly)
            m_value = raw & ADDR_MASK;
        }

        // Pointer semantics
        T* get() const {
            return reinterpret_cast<T*>(m_value & ADDR_MASK);
        }

        T& operator*() const {
            return *get();
        }

        T* operator->() const {
            return get();
        }

        // Metadata Accessors
        
        bool is_dirty() const {
            return (m_value & DIRTY_BIT_MASK) != 0;
        }
        
        void set_dirty(bool dirty) {
            if (dirty) m_value |= DIRTY_BIT_MASK;
            else       m_value &= ~DIRTY_BIT_MASK;
        }

        uint8_t get_quantization_level() const {
            return (m_value & QUANT_MASK) >> QUANT_SHIFT;
        }
        
        void set_quantization_level(uint8_t level) {
            // max 3 bits (0-7)
            level &= 0x7; 
            m_value &= ~QUANT_MASK; // clear
            m_value |= (static_cast<uintptr_t>(level) << QUANT_SHIFT);
        }
        
        uint16_t get_access_count() const {
            return (m_value & ACCESS_MASK) >> ACCESS_SHIFT;
        }
        
        void set_access_count(uint16_t count) {
            // max 12 bits (0-4095)
            count &= 0xFFF;
            m_value &= ~ACCESS_MASK; // clear
            m_value |= (static_cast<uintptr_t>(count) << ACCESS_SHIFT);
        }

        void increment_access() {
            uint16_t current = get_access_count();
            if (current < 0xFFF) {
                set_access_count(current + 1);
            }
        }

    private:
        uintptr_t m_value;
    };

}
