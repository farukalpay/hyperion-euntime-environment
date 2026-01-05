#pragma once

#include <atomic>
#include <vector>
#include <optional>
#include <concepts>
#include <cstddef>
#include <new>

namespace Hyperion::Core {

    /**
     * @brief A Lock-Free Single-Producer Single-Consumer (SPSC) Ring Buffer.
     * 
     * Uses std::atomic with explicit memory ordering (Acquire/Release) to ensure
     * that data written by the producer is visible to the consumer without
     * the overhead of a full sequential consistency fence or mutex.
     * 
     * CACHE_LINE_SIZE padding is used to prevent False Sharing between
     * the head and tail indices, which are updated by different threads.
     */
    template<typename T, size_t Capacity>
    class LockFreeRingBuffer {
        static_assert(Capacity > 0 && ((Capacity & (Capacity - 1)) == 0), "Capacity must be a power of 2");
        
        static constexpr size_t CACHE_LINE_SIZE = 64; // Common cache line size

    public:
        LockFreeRingBuffer() : m_head(0), m_tail(0) {}

        // Non-copyable, non-movable for simplicity
        LockFreeRingBuffer(const LockFreeRingBuffer&) = delete;
        LockFreeRingBuffer& operator=(const LockFreeRingBuffer&) = delete;

        /**
         * @brief Pushes an item into the buffer.
         * Thread Safety: Only callable by the PRODUCER thread.
         * 
         * @param item The item to push.
         * @return true if successful, false if buffer is full.
         */
        bool push(const T& item) {
            const size_t current_tail = m_tail.load(std::memory_order_relaxed);
            const size_t next_tail = (current_tail + 1) & (Capacity - 1);
            
            // Acquire load ensure we see the latest updates from consumer
            const size_t current_head = m_head.load(std::memory_order_acquire);

            if (next_tail == current_head) {
                return false; // Full
            }

            m_buffer[current_tail] = item;

            // Release store ensures the consumer sees the data write BEFORE seeing the index update
            m_tail.store(next_tail, std::memory_order_release);
            return true;
        }

        /**
         * @brief Pops an item from the buffer.
         * Thread Safety: Only callable by the CONSUMER thread.
         * 
         * @return std::optional<T> The item, or nullopt if empty.
         */
        std::optional<T> pop() {
            const size_t current_head = m_head.load(std::memory_order_relaxed);
            
            // Acquire load ensures we see the data written by producer before seeing the index update
            const size_t current_tail = m_tail.load(std::memory_order_acquire);

            if (current_head == current_tail) {
                return std::nullopt; // Empty
            }

            T item = m_buffer[current_head];

            const size_t next_head = (current_head + 1) & (Capacity - 1);
            
            // Release store ensures the producer sees we've consumed the slot
            m_head.store(next_head, std::memory_order_release);
            
            return item;
        }
        
        // Peek at the front item without removing it
        const T* peek() const {
             const size_t current_head = m_head.load(std::memory_order_relaxed);
             const size_t current_tail = m_tail.load(std::memory_order_acquire);
             
             if (current_head == current_tail) {
                 return nullptr;
             }
             return &m_buffer[current_head];
        }

    private:
        // Cache line padding to prevent false sharing
        alignas(CACHE_LINE_SIZE) std::atomic<size_t> m_head;
        alignas(CACHE_LINE_SIZE) std::atomic<size_t> m_tail;
        
        // The buffer itself
        alignas(CACHE_LINE_SIZE) T m_buffer[Capacity];
    };

}
