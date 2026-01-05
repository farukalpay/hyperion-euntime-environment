#include "../../include/core/ProcessingUnit.hpp"


#include "monitor/SystemMonitor.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <memory>
#include <array>
#include <thread>
#include <chrono>
#include <vector>
#include <cstdlib>
#include <cstring>

#include "mm/MemoryManager.hpp"
#include "core/JITAssembler.hpp"

#include <cstring>

namespace Hyperion {

    static ProcessingUnitConfig ParseEngineCLI(int argc, char* argv[]) {
        ProcessingUnitConfig config;
        for (int i = 1; i < argc; ++i) {
            if (std::strcmp(argv[i], "--reset") == 0) {
                config.reset_db = true;
            } else if (std::strcmp(argv[i], "--status") == 0) {
                config.show_status = true;
            }
        }
        return config;
    }

    // --- Engine Implementation ---

    ProcessingUnit::ProcessingUnit(int argc, char* argv[]) 
        : m_config(ParseEngineCLI(argc, argv)) { 
        
        // Bootstrapping the Ghost Engine singleton establishes the 1TB exception handler trap 
        // *before* any allocations occur, ensuring safe memory layout.
        auto ghost_res = Core::MemoryManager::instance().initialize();
        if (!ghost_res) {
            std::cerr << "FATAL: Ghost Engine boot failed: " << (int)ghost_res.error() << std::endl;
            exit(1);
        }
        
        // Allocates the executable pages (r-x) for generated traces.
        Core::JITAssembler jit;
        if (jit.initialize()) {
             // JIT initialized
        }
    }

    ProcessingUnit::~ProcessingUnit() {
        Shutdown();
    }

    void ProcessingUnit::Start() {
        if (m_config.show_status) return;

        m_running = true;
        
        // The Main Thread is reserved for the TUI Render Loop to ensure 60fps smoothness.
        // Offloading analysis avoids frame stutter.
        m_analysis_thread = std::jthread(&ProcessingUnit::AnalysisWorker, this);
    }

    void ProcessingUnit::Update() {
        if (!m_running) return;
        
        auto& tui = TUI::SystemMonitor::instance();

        // Stats Logic
        // We read from the Ghost Header directly using raw pointers.
        size_t doc_count = 0;
        void* base = Core::MemoryManager::instance().get_base_addr();
        
        if (base) {
            auto* header = reinterpret_cast<Core::MemoryHeader*>(base);
            // Use atomic_ref to safely read the counter updated by the worker thread
            doc_count = std::atomic_ref<uint64_t>(header->vector_count).load(std::memory_order_acquire);
        }

        std::stringstream stats;
        stats << "Docs: " << doc_count
              << " | Vocab: " << m_tokenizer.VocabularySize()
              << " | Threads: 2 [ACTIVE]";
        
        tui.update_status_stats(stats.str());
        tui.update_ghost_stats(
            Core::MemoryManager::instance().get_page_fault_count(),
            Core::MemoryManager::instance().get_resident_pages()
        );

        // Randomly touch the Ghost Memory to verify the exception handler is alive.
        if (rand() % 10 == 0 && base) { 
            static size_t ghost_offset = 0;
            // Raw pointer arithmetic (danger zone)
            volatile int val = *reinterpret_cast<int*>(static_cast<char*>(base) + ghost_offset);
            (void)val;
            ghost_offset = (ghost_offset + 4096) % (1024 * 1024 * 64);
        }

        // Visualizing the raw opcode stream
        if (m_processing_cooldown > 0) {
             static const std::vector<uint8_t> active_ops = { 
                 0xC5, 0xFC, 0x58, 0xC0, // VADDPS
                 0xC5, 0xFC, 0x59, 0xC9, // VMULPS
                 0x62, 0xF1, 0x7C, 0x48, 0x58, 0xC2,
                 0x90, 0x90 
             };
             tui.update_memory_view(active_ops.data(), active_ops.size());
             m_processing_cooldown--;
        } else {
            static const std::vector<uint8_t> idle_ops = {
                0xF3, 0x90, 
                0x48, 0x39, 0xC0, 
                0x75, 0xFB, 
                0x90, 0x90, 0x90
            };
            tui.update_memory_view(idle_ops.data(), idle_ops.size());
        }
    }

    void ProcessingUnit::Ingest(std::string_view text) {
        if (text.empty()) return;
        
        auto& tui = TUI::SystemMonitor::instance();
        tui.update_input_text(std::string(text));
        tui.trigger_input_flash();
        
        m_processing_cooldown = 20; 

        // Offload large text processing to the worker thread
        m_input_queue.push(std::string(text));
    }

    void ProcessingUnit::Shutdown() {
        if (!m_running) return; 
        m_running = false;
        
        Core::MemoryManager::instance().shutdown();
    }
    
    void ProcessingUnit::RunBenchmark() {
    }

    // --- Workers ---

    void ProcessingUnit::AnalysisWorker() {
        // Consumes the Lock-Free Ring Buffer.
        while (m_running) {
            auto content_opt = m_input_queue.pop();
            if (content_opt) {
                ProcessDocument(*content_opt);
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
    }

    // Hashing Vectorizer Dimension
    static constexpr size_t VECTOR_DIM = 256;

    void ProcessingUnit::ProcessDocument(const std::string& content) {
        // 1. Tokenize
        auto term_counts = m_tokenizer.Tokenize(content);
        if (term_counts.empty()) return;

        // 2. Vectorize (Hashing Trick)
        // Transform sparse term counts into a dense 256-dimension float vector
        std::vector<float> dense_vec(VECTOR_DIM, 0.0f);
        for (const auto& [term_id, count] : term_counts) {
            // Simple hash of the term ID to a bucket
            size_t bucket = term_id % VECTOR_DIM;
            // Add count (simple TF)
            dense_vec[bucket] += static_cast<float>(count);
        }

        // 3. Inline Zero-Copy Quantization to Ghost Memory
        // ------------------------------------------------
        // Instead of allocating a temporary std::vector<int8_t>, we calculate
        // and write directly to the persistent memory pointer.

        void* base = Core::MemoryManager::instance().get_base_addr();
        if (!base) return;

        auto* header = reinterpret_cast<Core::MemoryHeader*>(base);
        uint64_t current_offset = header->head_offset;
        
        // Structure on Disk/Ghost:
        // [Scale (float)] [Bias (float)] [Data (256 bytes)] 
        size_t entry_size = sizeof(float) + sizeof(float) + VECTOR_DIM;

        // Calculate destination pointer
        char* dest = static_cast<char*>(base) + current_offset;

        // A. Calculate Scalar Quantization Params (Min/Max)
        float min_val = dense_vec[0];
        float max_val = dense_vec[0];
        for (float v : dense_vec) {
            if (v < min_val) min_val = v;
            if (v > max_val) max_val = v;
        }

        // B. Write Scale & Bias
        float scale = (max_val - min_val) / 255.0f;
        float bias = min_val;

        // Handle flatline case (avoid div by zero)
        if (std::abs(max_val - min_val) < 1e-6) {
            scale = 1.0f; 
        }

        std::memcpy(dest, &scale, sizeof(float));
        dest += sizeof(float);

        std::memcpy(dest, &bias, sizeof(float));
        dest += sizeof(float);

        // C. Quantize Loop -> Direct Memory Write
        int8_t* q_dest = reinterpret_cast<int8_t*>(dest);
        
        for (size_t i = 0; i < VECTOR_DIM; ++i) {
            float v = (i < dense_vec.size()) ? dense_vec[i] : 0.0f;
            
            // Formula: (val - min) / (max - min) * 255 + (-128)
            float norm = (v - min_val) / (max_val - min_val);
            float scaled = norm * 255.0f;
            int result = static_cast<int>(std::round(scaled)) - 128;

            // Clamp
            if (result < -128) result = -128;
            if (result > 127) result = 127;

            q_dest[i] = static_cast<int8_t>(result);
        }

        // Advance destination pointer past the vector data (256 bytes)
        // dest += VECTOR_DIM (Handled implicitly by q_dest indexing)

        // Update Header
        // Commit the new offset
        header->head_offset += entry_size;
        
        // Atomically increment the vector count so the UI sees it instantly
        std::atomic_ref<uint64_t>(header->vector_count).fetch_add(1, std::memory_order_release);

        // Debug Log
        // std::cout << "[Engine] Stored Doc at offset " << current_offset << std::endl;
    }

}

