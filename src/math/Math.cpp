#include <cstdint>
#include <cstddef>
#include <cctype>

#if defined(__aarch64__) || defined(_M_ARM64)
    #include <arm_neon.h>
#endif

namespace Hyperion::Math {

     int32_t SIMD_Dot_Int8(const int8_t* a, const int8_t* b, size_t count) {
        int32_t accumulator = 0;
        size_t i = 0;

        #if defined(__aarch64__) || defined(_M_ARM64)
        // NEON Implementation
        // Process 16 bytes at a time (128-bit register)
        int32x4_t vec_acc = vdupq_n_s32(0);
        
        size_t loop_end = count & ~15; // Align to 16
        for (; i < loop_end; i += 16) {
            int8x16_t vec_a = vld1q_s8(a + i);
            int8x16_t vec_b = vld1q_s8(b + i);
            
            // Multiply and accumulate into 16-bit lanes, then widen to 32
            // vdotq_s32 (Dot Product) is available on newer ARMv8.2 (Apple Silicon M1+)
            #if defined(__ARM_FEATURE_DOTPROD)
                vec_acc = vdotq_s32(vec_acc, vec_a, vec_b);
            #else
                // Fallback for older ARM64 (widen to 16, mult, add)
                int16x8_t mul_lo = vmull_s8(vget_low_s8(vec_a), vget_low_s8(vec_b));
                int16x8_t mul_hi = vmull_s8(vget_high_s8(vec_a), vget_high_s8(vec_b));
                
                int32x4_t sum_lo = vpaddlq_s16(mul_lo);
                int32x4_t sum_hi = vpaddlq_s16(mul_hi);
                
                vec_acc = vaddq_s32(vec_acc, vaddq_s32(sum_lo, sum_hi));
            #endif
        }
        
        // Reduce the vector accumulator
        accumulator = vaddvq_s32(vec_acc);

        #endif

        // Scalar fallback / tail handling
        for (; i < count; ++i) {
            accumulator += static_cast<int32_t>(a[i]) * static_cast<int32_t>(b[i]);
        }

        return accumulator;
    }

} // namespace Hyperion::Math
