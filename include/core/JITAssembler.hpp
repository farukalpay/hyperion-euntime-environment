#pragma once

#include <vector>
#include <cstdint>
#include <sys/mman.h>
#include <cstring>
#include <iostream>
#include <unistd.h>
#include <expected>
#include <system_error>
#include "JITEmitter.hpp"

namespace Hyperion::Core {

    enum class JITError {
        None = 0,
        MmapFailed,
        MemoryProtectionFailed,
        CodeTooLarge,
        CacheInvalidationFailed
    };

    class JITAssembler {
    public:
        using JITFunc = uint64_t(*)();

        JITAssembler() = default;

        ~JITAssembler() {
            if (m_code_ptr && m_code_ptr != MAP_FAILED) {
                munmap(m_code_ptr, m_page_size);
            }
        }
        
        [[nodiscard]] std::expected<void, JITError> initialize() {
            auto res = alloc_exec_page();
            if (!res) return std::unexpected(res.error());
            
            return emit_machine_code();
        }

        JITFunc get_test_function() const {
             return reinterpret_cast<JITFunc>(m_code_ptr);
        }

    private:
        void* m_code_ptr = nullptr;
        size_t m_page_size = 0;
        JITEmitter m_emitter;

        std::expected<void, JITError> alloc_exec_page() {
            m_page_size = sysconf(_SC_PAGESIZE);
            
            // 1. Allocate as READ | WRITE first to write the code
            void* ptr = mmap(nullptr, m_page_size, PROT_READ | PROT_WRITE, 
                             MAP_PRIVATE | MAP_ANON, -1, 0);
                             
            if (ptr == MAP_FAILED) {
                return std::unexpected(JITError::MmapFailed);
            }
            m_code_ptr = ptr;
            return {};
        }

        std::expected<void, JITError> finalize_page() {
            // 2. Switch to READ | EXECUTE
            if (mprotect(m_code_ptr, m_page_size, PROT_READ | PROT_EXEC) == -1) {
                return std::unexpected(JITError::MemoryProtectionFailed);
            }
            
            // Clear instruction cache (critical for ARM64)
            #if defined(__aarch64__) && defined(__APPLE__)
                __builtin___clear_cache((char*)m_code_ptr, (char*)m_code_ptr + m_page_size);
            #endif
            return {};
        }

        std::expected<void, JITError> emit_machine_code() {
            // Generate: return 0xDEADBEEFCAFEBABE;
            
            #if defined(__x86_64__)
                m_emitter.emit_mov_reg_imm64(Reg::RAX, 0xDEADBEEFCAFEBABE);
                m_emitter.emit_ret();
            #elif defined(__aarch64__)
                m_emitter.emit_mov_reg_imm64(Reg::R0, 0xDEADBEEFCAFEBABE);
                m_emitter.emit_ret();
            #endif
            
            const auto& code = m_emitter.get_code();

            // Write to buffer
            if (code.size() > m_page_size) {
                 return std::unexpected(JITError::CodeTooLarge);
            }
            std::memcpy(m_code_ptr, code.data(), code.size());
            
            return finalize_page();
            // std::cout << "[JIT] Generated " << code.size() << " bytes of machine code at " << m_code_ptr << std::endl;
        }
    };

}
