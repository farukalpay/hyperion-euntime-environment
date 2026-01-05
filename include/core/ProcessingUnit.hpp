#pragma once

#include <thread>
#include <atomic>
#include <queue>
#include <condition_variable>
#include <functional>
#include <optional>

#include "core/Tokenizer.hpp"
#include "core/LockFreeRingBuffer.hpp"

namespace Hyperion {

    struct ProcessingUnitConfig {
        bool reset_db = false;
        bool show_status = false;
        bool debug_mode = false;
    };

    class ProcessingUnit {
    public:
        ProcessingUnit(int argc, char* argv[]);
        ~ProcessingUnit();

        void Start();
        void Update(); 
        void Ingest(std::string_view text);
        void Shutdown();
        void RunBenchmark();

    private:
        ProcessingUnitConfig m_config;
        Tokenizer m_tokenizer;
        IDFManager m_idf_manager;
        
        std::atomic<bool> m_running{false};
        int m_processing_cooldown = 0;

        // Lock-Free Single-Producer Single-Consumer Ring Buffer for IPC
        Core::LockFreeRingBuffer<std::string, 64> m_input_queue;

        std::jthread m_analysis_thread;

        void AnalysisWorker();
        void ProcessDocument(const std::string& content);
    };

    // --- IDF Manager (Inlined) ---
    class IDFManager {
    public:
        void UpdateDocs(const std::vector<TermID>& unique_terms_in_doc) {
             for (auto tid : unique_terms_in_doc) {
                m_term_doc_freqs[tid]++;
            }
        }
        
        float GetIDF(TermID term_id, size_t total_docs) const {
            if (total_docs == 0) return 0.0f;
            auto it = m_term_doc_freqs.find(term_id);
            uint32_t df = (it != m_term_doc_freqs.end()) ? it->second : 0;
            return std::log(static_cast<float>(total_docs) / (1.0f + df)) + 1.0f;
        }

        const std::unordered_map<TermID, uint32_t>& GetDocFreqs() const { return m_term_doc_freqs; }
        void SetDocFreqs(const std::unordered_map<TermID, uint32_t>& freqs) { m_term_doc_freqs = freqs; }
        
    private:
        std::unordered_map<TermID, uint32_t> m_term_doc_freqs;
    };

} // namespace Hyperion


