# Changelog

All notable changes to the Cognitron Zero project will be documented in this file.

## [1.0.0] - "The Singularity Release"
**Released**: 2026-01-05

### 🚀 Highlights
*   **Core**: Implemented Userspace Fiber Scheduler using raw Assembly Context Switching (`switch.S`) for both x86_64 (System V AMD64 ABI) and ARM64 (AAPCS64).
*   **Memory**: Fixed Critical Integer Overflow bug allowing full **1TB Ghost Region** allocation on macOS/ARM64.
*   **Arch**: Added native ARM64 `SIGBUS` trap handling to ensure stability on Apple Silicon (M1/M2/M3).
*   **UI**: Integrated Retina TUI with real-time Fiber visualization, Simplex Noise monitoring, and JIT Stream hex dumps.

### Added
- **`src/kernel/arch/`**: Assembly context switchers for fiber management.
- **`src/kernel/Scheduler.cpp`**: Cooperative multitasking nucleus with strict null-pointer safety checks.
- **`docs/internals/`**: Deep-dive architecture documentation (Ghost Memory, ABI Contracts).
- **`make clean`**: Added rule to force-rebuild artifacts safely.

### Fixed
- **GhostEngine**: `1ULL << 40` corrected virtual memory reservation logic.
- **Nucleus**: Patched `SIGSEGV at 0x0` by correcting ARM64 stack register push order (X30 Link Register restoration).
- **TUI**: Resolved box-drawing character encoding issues by enforcing `en_US.UTF-8` locale.

### Security
- Added `PROT_NONE` reservation to prevent accidental memory corruption outside authenticated pages.
- Added stack canaries (implicit via `mmap` guard pages) between fibers.
