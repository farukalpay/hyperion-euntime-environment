#pragma once

#include <vector>
#include <string>
#include <vector>
#include <atomic>
#include <cstdint>
#include <expected>
#include <thread>
#include <termios.h>

namespace Hyperion::TUI {

    struct Point {
        int x, y;
    };

    struct Rect {
        int x, y, w, h;
    };

    class SystemMonitor {
    public:
        static SystemMonitor& instance();

        SystemMonitor();
        ~SystemMonitor();

        // Prevent copying
        SystemMonitor(const SystemMonitor&) = delete;
        SystemMonitor& operator=(const SystemMonitor&) = delete;

        [[nodiscard]] std::expected<void, std::string> initialize();
        void shutdown();

        // Main Loop / Render Frame
        void render(); 

        // Immediate Mode Drawing API
        void clear_buffer();
        void draw_text(int x, int y, const std::string& text, const std::string& ansi_color = "");
        void draw_box(const Rect& rect);
        void draw_line(int x0, int y0, int x1, int y1);
        void draw_ghost_map(const Rect& rect, const std::vector<uint8_t>& residency_map);
        void draw_jit_stream(const Rect& rect, const std::vector<uint8_t>& code_bytes);
        
        // Data Feeders (Thread Safe)
        void set_header_info(const std::string& info);
        void update_status_stats(const std::string& stats);
        void update_ghost_stats(size_t faults, size_t resident);
        void update_simd_lanes(const float* lanes);
        void update_memory_view(const void* ptr, size_t size);
        void update_input_text(const std::string& text);
        void trigger_input_flash();

    private:
        void enable_raw_mode();
        void disable_raw_mode();
        
        void present();
        
        std::string compute_buffer_diff(const std::vector<std::string>& front, const std::vector<std::string>& back);

    private:
        int m_tty_fd = -1;
        struct termios m_orig_termios;
        
        std::atomic<bool> m_running = false;
        
        int m_width = 80;
        int m_height = 24;
        
        std::vector<std::string> m_front_buffer;
        std::vector<std::string> m_back_buffer;
        
        // State for Render Loop
        std::string m_header_info;
        std::string m_stats_info;
        std::string m_input_text;
        std::vector<uint8_t> m_ghost_map_cache;
        std::vector<uint8_t> m_jit_cache;
        std::vector<uint8_t> m_memory_cache;
        
        size_t m_page_faults = 0;
        size_t m_resident_pages = 0;
        
        std::atomic<int> m_flash_timer{0};

        // Liveness State
        std::vector<int> m_ghost_heat_map;
        int m_spinner_idx = 0;
        
        // Randomness for "Noise"
        // We will use a simple LCG or rand() for simplicity to avoid heavy headers if not needed, 
        // but <random> is better. Let's include <random> at the top or use simple rand wrapper.
        // Actually, let's just use a simple member helper to keep header clean if we don't want <random> in header.
        // But implementing <random> is cleaner.
        
        // Let's defer <random> to implementation to avoid recompiling everything including this header.
        // We will just store the last random value or state if needed, or just use rand() in cpp.
        // For C++23, we should try to be clean.
        // We'll add a helper private method for random numbers.
        uint32_t m_rand_seed = 0;
        uint32_t fast_rand(); 
    };

}
