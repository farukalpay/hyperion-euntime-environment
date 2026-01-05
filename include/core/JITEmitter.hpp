#pragma once

#include <vector>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <sys/mman.h>

namespace Hyperion::Core {

    // Simple Register Enum (mapping to hardware indices)
    enum class Reg : uint8_t {
        // x86-64: RAX=0, RCX=1, RDX=2, RBX=3, RSP=4, RBP=5, RSI=6, RDI=7
        // ARM64: X0=0, X1=1 ... X7=7
        R0 = 0,
        R1 = 1,
        R2 = 2,
        R3 = 3,
        R4 = 4,
        R5 = 5,
        R6 = 6,
        R7 = 7
    };

    class JITEmitter {
    public:
        JITEmitter() = default;

        const std::vector<uint8_t>& get_code() const { return m_code; }

        // Returns current offset (label)
        size_t current_offset() const { return m_code.size(); }

        // Emit: MOV Reg, Immediate (64-bit)
        void emit_mov_reg_imm64(Reg reg, uint64_t imm) {
            #if defined(__x86_64__)
                // REX.W + B8+rd + imm64
                // REX.W (48) is needed for 64-bit operand sizing
                // Opcode B8 + reg index
                uint8_t rex = 0x48;
                uint8_t opcode = 0xB8 + static_cast<uint8_t>(reg);
                
                m_code.push_back(rex);
                m_code.push_back(opcode);
                emit_bytes(&imm, 8);
                
            #elif defined(__aarch64__)
                // ARM64: Need MOVZ + 3 MOVKs
                // MOVZ Xd, #imm16, LSL #0
                uint32_t imm0 = imm & 0xFFFF;
                emit_arm_movz(reg, imm0, 0);
                
                // MOVK Xd, #imm16, LSL #16
                uint32_t imm1 = (imm >> 16) & 0xFFFF;
                if (imm1 != 0 || (imm >> 16) != 0) { // Optimize? No, deep-fill says strict.
                    emit_arm_movk(reg, imm1, 1);
                }

                uint32_t imm2 = (imm >> 32) & 0xFFFF;
                if (imm2 != 0 || (imm >> 32) != 0) {
                    emit_arm_movk(reg, imm2, 2);
                }

                uint32_t imm3 = (imm >> 48) & 0xFFFF;
                if (imm3 != 0) {
                    emit_arm_movk(reg, imm3, 3);
                }
            #else
                throw std::runtime_error("Unsupported Architecture");
            #endif
        }

        // Emit: ADD Dest, Src (Dest += Src)
        void emit_add_reg_reg(Reg dst, Reg src) {
             #if defined(__x86_64__)
                // ADD dst, src
                // REX.W + 01 + ModR/M
                // 0x48 0x01 ...
                // ModR/M: 11 (Reg addressing) | src (3 bits) | dst (3 bits)
                uint8_t rex = 0x48;
                uint8_t opcode = 0x01;
                uint8_t modrm = 0xC0 | (static_cast<uint8_t>(src) << 3) | static_cast<uint8_t>(dst);
                
                m_code.push_back(rex);
                m_code.push_back(opcode);
                m_code.push_back(modrm);
                
             #elif defined(__aarch64__)
                // ADD Xd, Xn, Xm
                // Opcode: 10001011000 + m + sh + n + d
                // Simplified: ADD Xd, Xn, Xm (64-bit) -> 0x8B000000 base
                // Xn is treated as dst here for accumulation (dst = dst + src)
                
                // Encoding: sf=1 (bit 31), op=0, S=0, shift=00
                // 1000 1011 000m mmmm 0000 00nn nnnd dddd
                // Hex: 8B000000 | (src << 16) | (dst << 5) | dst
                // Wait, ADD <Xd>, <Xn>, <Xm>
                // We want dst = dst + src
                // So Xd = dst, Xn = dst, Xm = src
                
                uint32_t instr = 0x8B000000;
                instr |= (static_cast<uint32_t>(src) << 16); // Rm
                instr |= (static_cast<uint32_t>(dst) << 5);  // Rn
                instr |= (static_cast<uint32_t>(dst));       // Rd
                
                emit_bytes(&instr, 4);
             #endif
        }

        // Emit: RET
        void emit_ret() {
            #if defined(__x86_64__)
                m_code.push_back(0xC3);
            #elif defined(__aarch64__)
                // RET X30 (LR) -> D65F03C0
                uint32_t instr = 0xD65F03C0;
                emit_bytes(&instr, 4);
            #endif
        }

    private:
        std::vector<uint8_t> m_code;

        void emit_bytes(const void* data, size_t size) {
            const uint8_t* bytes = static_cast<const uint8_t*>(data);
            m_code.insert(m_code.end(), bytes, bytes + size);
        }

        #if defined(__aarch64__)
        // ARM64 Helpers
        void emit_arm_movz(Reg reg, uint16_t imm, int shift_block) {
            // MOVZ: 32-bit: 0x52800000, 64-bit: 0xD2800000 (sf=1)
            // 1 10 10010 1 hw  imm16  Rd
            // sf=1, opc=10 (MOVZ)
            // hw = shift_block (0=0, 1=16, 2=32...)
            
            uint32_t instr = 0xD2800000;
            instr |= (shift_block << 21);
            instr |= (imm << 5);
            instr |= static_cast<uint32_t>(reg);
            
            emit_bytes(&instr, 4);
        }

        void emit_arm_movk(Reg reg, uint16_t imm, int shift_block) {
            // MOVK: 64-bit: 0xF2800000
            // 1 11 10010 1 hw imm16 Rd
            
            uint32_t instr = 0xF2800000;
            instr |= (shift_block << 21);
            instr |= (imm << 5);
            instr |= static_cast<uint32_t>(reg);
            
            emit_bytes(&instr, 4);
        }
        #endif
    };

}
