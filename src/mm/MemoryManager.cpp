#include "mm/MemoryManager.hpp"
#include <iostream>
#include <sys/mman.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <signal.h>
#include <cstdlib>

// ARCHITECTURAL NOTE:
// This signal handler acts as a User-Space "Micro-Kernel" trap.
// It intercepts invalid memory accesses (SIGBUS/SIGSEGV) within the Ghost Region
// and lazy-loads the backing pages, emulating infinite memory.
static void ghost_signal_handler(int sig, siginfo_t* info, void* ctx) {
    if (sig != SIGSEGV && sig != SIGBUS) {
        return; // Not our problem
    }

    void* fault_addr = info->si_addr;
    auto& engine = Hyperion::Core::MemoryManager::instance();
    void* base = engine.get_base_addr();
    
    // BOUNDS CHECK & RECOVERY:
    // If the fault is within [Base, Base + 1TB], we materialize the page (Lazy Alloc).
    uintptr_t fault_val = (uintptr_t)fault_addr;
    uintptr_t base_val = (uintptr_t)base;
    uintptr_t end_val = base_val + Hyperion::Core::MemoryManager::GHOST_SPACE_SIZE;
    (void)ctx; // Suppress unused parameter warning

    if (base && fault_val >= base_val && fault_val < end_val) {
        if (engine.handle_fault(fault_addr)) {
            return; // Resume execution seamlessly (User code sees nothing)
        }
    }

    // CRITICAL:
    // If we reach here, it's a genuine crash (Segfault/Bus Error) outside our jurisdiction.
    // Restoration of default handler ensures a proper core dump generation.
    signal(sig, SIG_DFL);
    std::cerr << "[MemoryManager] FATAL: Unhandled " << (sig == SIGSEGV ? "SIGSEGV" : "SIGBUS") 
              << " at " << fault_addr << std::endl;
}

namespace Hyperion::Core {

    MemoryManager& MemoryManager::instance() {
        static MemoryManager instance;
        return instance;
    }

    MemoryManager::MemoryManager() {
    }

    MemoryManager::~MemoryManager() {
        shutdown();
    }

    std::expected<void, RuntimeError> MemoryManager::initialize() {
        if (m_running) return {};
        
        std::cout << "[MemoryManager] Initializing 1TB Ghost Memory..." << std::endl;
        
        // 1. Reserve VM Address Space
        auto res = reserve_address_space();
        if (!res) return std::unexpected(res.error());
        
        // 2. Install Signal Handlers (SIGSEGV/SIGBUS)
        auto sig_res = install_signal_handlers();
        if (!sig_res) return std::unexpected(sig_res.error());
        
        m_running = true;

        // 3. Initialize/Load Header
        initialize_header();

        std::cout << "[MemoryManager] Systems Online. Ghost Mode Active." << std::endl;
        return {};
    }

    void MemoryManager::shutdown() {
        if (!m_running) return;
        
        std::cout << "[MemoryManager] Shutting down..." << std::endl;
        m_running = false;
        
        if (m_base_addr != MAP_FAILED && m_base_addr != nullptr) {
            munmap(m_base_addr, GHOST_SPACE_SIZE);
        }
    }

    std::expected<void, RuntimeError> MemoryManager::reserve_address_space() {
        // 1TB Reservation.
        int flags = MAP_PRIVATE | MAP_ANON;
        #ifdef MAP_NORESERVE
        flags |= MAP_NORESERVE;
        #endif

        // mmap with PROT_NONE to reserve but not commit
        m_base_addr = mmap(nullptr, GHOST_SPACE_SIZE, PROT_NONE, flags, -1, 0);

        if (m_base_addr == MAP_FAILED) {
            std::cerr << "Mmap failed (PROT_NONE): " << strerror(errno) << " (" << errno << ")" << std::endl;
             return std::unexpected(RuntimeError::MemoryReservationFailed);
        }

        std::cout << "[MemoryManager] Reserved " << (GHOST_SPACE_SIZE/1024/1024/1024) << "GB at " << m_base_addr << std::endl;
        return {};
    }

    std::expected<void, RuntimeError> MemoryManager::install_signal_handlers() {
        struct sigaction sa;
        sa.sa_flags = SA_SIGINFO | SA_NODEFER; 
        sa.sa_sigaction = ghost_signal_handler;
        sigemptyset(&sa.sa_mask);

        if (sigaction(SIGBUS, &sa, nullptr) == -1) {
            return std::unexpected(RuntimeError::InitializationFailed);
        }
        if (sigaction(SIGSEGV, &sa, nullptr) == -1) {
             return std::unexpected(RuntimeError::InitializationFailed);
        }
        
        return {};
    }

    std::expected<void*, RuntimeError> MemoryManager::get_ghost_ptr(size_t offset) {
        if (offset >= GHOST_SPACE_SIZE) return std::unexpected(RuntimeError::InvalidAccess);
        if (!m_base_addr) return std::unexpected(RuntimeError::InitializationFailed);
        
        return static_cast<char*>(m_base_addr) + offset;
    }

    void MemoryManager::initialize_header() {
        // Access 0x0 offset to trigger fault and create the page
        // Since this is RAM based, it's always "new" on boot, but we structure it correctly.
        
        auto ptr_res = get_ghost_ptr(0);
        if (!ptr_res) {
            std::cerr << "[MemoryManager] Failed to get header pointer." << std::endl;
            return;
        }

        MemoryHeader* header = static_cast<MemoryHeader*>(*ptr_res);

        // BOOTSTRAP TRAP:
        // Accessing 'header->magic' at offset 0 forces the first Page Fault.
        // This validates the entire signal handling pipeline before any logic runs.
        
        if (header->magic != GHOST_MAGIC) {
            std::cout << "[MemoryManager] No existing header found (Volatile RAM). Initializing..." << std::endl;
            header->magic = GHOST_MAGIC;
            header->vector_count = 0;
            header->head_offset = sizeof(MemoryHeader);
        } else {
             std::cout << "[MemoryManager] Existing header found (Persistent?)" << std::endl;
        }
    }

    void MemoryManager::run_self_test() {
        std::cout << "[MemoryManager] Running Self-Test..." << std::endl;
        
        size_t offset_idx = 512ULL * 1024 * 1024 * 1024;
        auto ptr_res = get_ghost_ptr(offset_idx);
        
        if (!ptr_res) {
            std::cerr << "Self-Test Failed: Could not get pointer." << std::endl;
            exit(1);
        }
        
        volatile int* magic_ptr = (int*)*ptr_res;
        
        std::cout << "[MemoryManager] Accessing Virtual Address: " << magic_ptr << std::endl;
        
        *magic_ptr = 9999;
        
        if (*magic_ptr == 9999) {
             std::cout << "[MemoryManager] SUCCESS! Magic write survived. Page materialized." << std::endl;
        } else {
             std::cerr << "[MemoryManager] CRITICAL FAILURE: Value mismatch." << std::endl;
             exit(1);
        }
    }

    bool MemoryManager::handle_fault(void* fault_addr) {
        // 1. Align to page boundary
        uintptr_t addr_val = (uintptr_t)fault_addr;
        size_t page_size = sysconf(_SC_PAGESIZE);
        uintptr_t page_addr = addr_val & ~(page_size - 1);
        
        // 2. Materialize the page via mprotect(PROT_READ | PROT_WRITE)
        if (mprotect((void*)page_addr, page_size, PROT_READ | PROT_WRITE) != 0) {
            std::cerr << "[MemoryManager] mprotect failed at " << (void*)page_addr 
                      << ": " << strerror(errno) << std::endl;
            return false;
        }

        m_fault_count.fetch_add(1, std::memory_order_relaxed);
        m_resident_pages.fetch_add(1, std::memory_order_relaxed);
        
        return true;
    }

}
