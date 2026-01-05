#!/bin/bash
echo "=== COGNITRON NUCLEUS VERIFICATION ==="

# Check for macOS otool
if command -v otool &> /dev/null; then
    echo "[*] Disassembling switch.o using otool..."
    otool -tvV obj/kernel/arch/switch.o 2>/dev/null || echo "Build the project first!"
else
    # Fallback to objdump (Linux)
    echo "[*] Disassembling switch.o using objdump..."
    objdump -d obj/kernel/arch/switch.o 2>/dev/null || echo "Build the project first!"
fi

echo "======================================"
echo "Check above for raw assembly instructions."
echo "CRITICAL: Look for PUSH/POP (x64) or STP/LDP (ARM64) sequences."
