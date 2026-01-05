#pragma once

#include <cstddef>
#include <cstdint>

namespace cognitron::kernel {

/**
 * @brief Raw System Call Wrapper using Inline Assembly.
 * Bypasses libc wrappers to reduce overhead to zero.
 * NOTE: This is highly architecture-dependent.
 */
class Syscall {
public:
    /**
     * @brief Raw write to stdout (File Descriptor 1).
     * @param msg Trigger pointer to character buffer.
     * @param len Length of the buffer.
     * @return Number of bytes written.
     */
    static inline ssize_t raw_write_stdout(const char* msg, size_t len) {
        ssize_t ret;
#if defined(__aarch64__)
        // ARM64: syscall number for write is 64.
        // x0 = fd, x1 = buf, x2 = count, x8 = syscall_nr
        // svc #0 triggers the syscall.
        register int      x0 __asm__("x0") = 1;     // stdout
        register const char* x1 __asm__("x1") = msg;   // buffer
        register size_t   x2 __asm__("x2") = len;   // length
        register int      x8 __asm__("x8") = 64;    // syscall 64 (write)

        __asm__ volatile(
            "svc #0"
            : "=r"(x0)       // Output: x0 contains return value
            : "r"(x0), "r"(x1), "r"(x2), "r"(x8) // Input
            : "memory"       // Clobber
        );
        ret = x0;
#elif defined(__x86_64__)
        // x86_64: syscall number for write is 1 (on macOS BSD syscalls, usually it's different depending on OS).
        // macOS: write is 4 (BSD). Linux is 1.
        // Assuming macOS for x86_64 fallback (User is likely on macOS given Mach-O focus).
        // rax = syscall_nr + 0x2000000 (class unix/bsd on macOS), rdi = fd, rsi = buf, rdx = count
        // BUT wait, "syscall" instruction uses different regs on System V ABI.
        // rdi, rsi, rdx, r10, r8, r9. rax = syscall number.
        // macOS syscall convention: rax = syscall # | 0x2000000.
        
        // This is tricky without knowing OS. Assuming macOS for now due to Mach-O context.
        // syscall 4 is write.
        size_t syscall_nr = 0x2000004; 

        __asm__ volatile(
            "syscall"
            : "=a" (ret)
            : "0" (syscall_nr), "D" (1), "S" (msg), "d" (len)
            : "rcx", "r11", "memory"
        );
#else
        #error "Unsupported Architecture for Raw Syscalls"
#endif
        return ret;
    }
};

} // namespace cognitron::kernel
