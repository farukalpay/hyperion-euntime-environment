#pragma once

#include <vector>
#include <string>
#include <atomic>
#include <thread>
#include <cstdint>
#include <expected> // C++23
#include <system_error>

namespace Hyperion::Core {

    enum class RuntimeError {
        None = 0,
        InitializationFailed,
        PortAllocationFailed,
        ThreadSpawnFailed,
        MemoryReservationFailed,
        InvalidAccess,
        OperatingSystemError
    };

    struct MemoryHeader {
        uint64_t magic;         // 0xC06N17R0N
        uint64_t vector_count;
        uint64_t head_offset;
    };

    /**
     * @brief The MemoryManager manages a massive virtual memory space (1 TB).
     * It uses POSIX Signal Handling (SIGSEGV/SIGBUS) to lazy-load pages.
     */
    class MemoryManager {
    public:
        // 1 TB Virtual Space (Explicit ULL to prevent overflow)
        static constexpr size_t GHOST_SPACE_SIZE = 1099511627776ULL;
        static constexpr uint64_t GHOST_MAGIC = 0xC06DFEEDDEADBEEFULL; 

        static MemoryManager& instance();

        MemoryManager();
        ~MemoryManager();

        // Initialize: Reserve VM, install Signal Handlers, and Check/Init Header
        [[nodiscard]] std::expected<void, RuntimeError> initialize();
        
        void shutdown();
        
        [[nodiscard]] std::expected<void*, RuntimeError> get_ghost_ptr(size_t offset);
        
        void run_self_test();

        size_t get_page_fault_count() const { return m_fault_count.load(std::memory_order_relaxed); }
        size_t get_resident_pages() const { return m_resident_pages.load(std::memory_order_relaxed); }

        // Helper for the static signal handler
        void* get_base_addr() const { return m_base_addr; }
        
        // The actual healing logic called by the signal handler
        bool handle_fault(void* fault_addr);

    private:
        std::expected<void, RuntimeError> reserve_address_space();
        std::expected<void, RuntimeError> install_signal_handlers();
        void initialize_header();

    private:
        void* m_base_addr = nullptr;
        std::atomic<bool> m_running = false;
        std::atomic<size_t> m_fault_count = 0;
        std::atomic<size_t> m_resident_pages = 0;
    };

}
