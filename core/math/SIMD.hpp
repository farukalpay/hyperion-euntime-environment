#pragma once

#include <vector>
#include <cmath>
#include <cstdint>
#include <arm_neon.h>

namespace Cognitron::Math {

    class SIMD {
    public:
        // Hardware Accelerated Dot Product (NEON)
        // Computes dot product of two float arrays.
        // Falls back to scalar if not multiples of 4 (though we should align).
        static float DotProduct(const float* a, const float* b, size_t count) {
            float32x4_t sum_vec = vdupq_n_f32(0.0f);
            size_t i = 0;

            // Unroll loop for 4 floats at a time (128-bit vector)
            for (; i + 3 < count; i += 4) {
                float32x4_t va = vld1q_f32(a + i);
                float32x4_t vb = vld1q_f32(b + i);
                sum_vec = vfmaq_f32(sum_vec, va, vb); // Fused Multiply-Add
            }

            // Horizontal add
            float sum = vaddvq_f32(sum_vec);

            // Handle leftovers
            for (; i < count; ++i) {
                sum += a[i] * b[i];
            }

            return sum;
        }

        // Quantized Dot Product (Int8) for SQ8
        // Using NEON instructions for signed 8-bit integers
        static int32_t DotProductInt8(const int8_t* a, const int8_t* b, size_t count) {
            int32x4_t sum_vec = vdupq_n_s32(0);
            size_t i = 0;

            // Process 16 bytes at a time (128-bit register)
            for (; i + 15 < count; i += 16) {
                int8x16_t va = vld1q_s8(a + i);
                int8x16_t vb = vld1q_s8(b + i);
                
                // Multiply and widen to 16-bit, then accumulate to 32-bit
                // NEON has specific dot product instructions in newer archs (vdot), 
                // but let's stick to safe widely supported ops or assume V8.2+ for vdot.
                // For broad compatibility, we might do widening manually.
                // BUT, user asked for "Hardware Acceleration", let's assume modern Apple Silicon (supports VDOT).
                
                #if defined(__ARM_FEATURE_DOTPROD)
                    sum_vec = vdotq_s32(sum_vec, va, vb);
                #else
                    // Fallback or just standard multiply-add logic
                    // This is complex in pure NEON without VDOT, simplified for this snippet:
                    // Just accept scalar loop for leftovers or non-vdot fallback is fine for "demo".
                    // But wait, the prompt asks explicitly to BEAT compiler.
                    // Let's implement the widening approach.
                    
                    int16x8_t prod_low = vmull_s8(vget_low_s8(va), vget_low_s8(vb));
                    int16x8_t prod_high = vmull_s8(vget_high_s8(va), vget_high_s8(vb));
                    
                    int32x4_t sum_low = vpaddlq_s16(prod_low);
                    int32x4_t sum_high = vpaddlq_s16(prod_high);
                    
                    sum_vec = vaddq_s32(sum_vec, vaddq_s32(sum_low, sum_high));
                #endif
            }

            int32_t sum = vaddvq_s32(sum_vec);

            for (; i < count; ++i) {
                sum += a[i] * b[i];
            }
            return sum;
        }
    };

} // namespace Cognitron::Math
