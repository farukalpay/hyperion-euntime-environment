#include "monitor/SystemMonitor.hpp"
#include "kernel/Scheduler.hpp"     // Access Kernel Scheduler
#include "mm/MemoryManager.hpp"   // Access Ghost Engine
#include <unistd.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <iostream>
#include <sstream>
#include <cstring>
#include <iomanip>
#include <cmath>
#include <clocale>
#include <chrono>

namespace Hyperion::TUI {

    namespace BoxChars {
        const std::string H_LINE = "\u2500"; // ─
        const std::string V_LINE = "\u2502"; // │
        const std::string TL     = "\u250c"; // ┌
        const std::string TR     = "\u2510"; // ┐
        const std::string BL     = "\u2514"; // └
        const std::string BR     = "\u2518"; // ┘
        // Heat Map Levels
        const std::string HEAT_5 = "\u2588"; // █ (White)
        const std::string HEAT_4 = "\u2593"; // ▓
        const std::string HEAT_3 = "\u2592"; // ▒
        const std::string HEAT_2 = "\u2591"; // ░
        const std::string HEAT_1 = " ";      //  
    }

    SystemMonitor& SystemMonitor::instance() {
        static SystemMonitor s_instance;
        return s_instance;
    }

    SystemMonitor::SystemMonitor() {
        m_tty_fd = STDOUT_FILENO;
        auto now = std::chrono::high_resolution_clock::now();
        m_rand_seed = static_cast<uint32_t>(now.time_since_epoch().count());
    }

    SystemMonitor::~SystemMonitor() {
        if (m_running) {
            shutdown();
        }
    }

    uint32_t SystemMonitor::fast_rand() {
        // Simple Xorshift32
        uint32_t x = m_rand_seed;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        m_rand_seed = x;
        return x;
    }

    std::expected<void, std::string> SystemMonitor::initialize() {
        struct winsize ws;
        if (ioctl(m_tty_fd, TIOCGWINSZ, &ws) == -1) {
             m_width = 80;
             m_height = 24;
        } else {
            m_width = ws.ws_col;
            m_height = ws.ws_row;
        }

        size_t size = m_width * m_height;
        // Init buffers with spaces
        m_front_buffer.assign(size, " ");
        m_back_buffer.assign(size, " ");

        enable_raw_mode();

        // Initial Clear
        write(m_tty_fd, "\033[?25l", 6); // Hide cursor
        write(m_tty_fd, "\033[2J", 4);   // Clear screen
        write(m_tty_fd, "\033[H", 3);    // Home cursor

        m_running = true;
        
        // Resize caches
        m_ghost_map_cache.resize(1024, 0); 
        m_ghost_heat_map.resize(1024, 0); // Corresponding heat
        m_jit_cache.resize(100, 0);

        return {};
    }

    void SystemMonitor::shutdown() {
        m_running = false;
        write(m_tty_fd, "\033[?25h", 6);
        write(m_tty_fd, "\033[0m", 4);
        write(m_tty_fd, "\033[2J", 4);
        write(m_tty_fd, "\033[H", 3);
        disable_raw_mode();
    }

    void SystemMonitor::enable_raw_mode() {
        if (tcgetattr(m_tty_fd, &m_orig_termios) == -1) return;
        struct termios raw = m_orig_termios;
        raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
        raw.c_iflag &= ~(IXON | ICRNL);
        raw.c_oflag &= ~(OPOST);
        raw.c_cflag |= (CS8);
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 1;
        tcsetattr(m_tty_fd, TCSAFLUSH, &raw);
    }

    void SystemMonitor::disable_raw_mode() {
        tcsetattr(m_tty_fd, TCSAFLUSH, &m_orig_termios);
    }

    // --- Drawing ---

    void SystemMonitor::render() {
        clear_buffer();

        // Update random seed entropy from input len if we wanted to, but time is fine.
        
        // Header
        draw_text(2, 0, m_header_info.empty() ? "COGNITRON ZERO UNIKERNEL" : m_header_info);
        
        // Status Bar
        std::stringstream ss_stats;
        auto& ghost = Hyperion::Core::MemoryManager::instance();
        ss_stats << "FAULTS: " << ghost.get_page_fault_count() 
                 << " | RESIDENT: " << ghost.get_resident_pages()
                 << " | FIBERS: " << Kernel::Scheduler::Get().AllFibers().size();
        draw_text(2, m_height - 1, ss_stats.str());


        // Layout: Left Panel (Ghost Map)
        // 50% width
        int gw = m_width / 2 - 2;
        int gh = m_height - 12; // Leave room for JIT or logs
        Rect rGhost = {1, 2, gw, gh};
        draw_ghost_map(rGhost, m_ghost_map_cache);


        // Layout: Right Panel (Fibers & Internals)
        Rect rFibers = {gw + 3, 2, gw, gh};
        draw_box(rFibers);
        draw_text(rFibers.x + 2, rFibers.y, " FIBER SCHEDULER ");
        
        int row = 1;
        const auto& fibers = Kernel::Scheduler::Get().AllFibers();
        Kernel::Fiber* current = Kernel::Scheduler::Get().Current();
        
        // Spinner for active fiber
        const char spinner[] = "|/-\\";
        m_spinner_idx = (m_spinner_idx + 1) % 4;

        for (const auto* f : fibers) {
            std::stringstream ss;
            bool is_curr = (f == current);
            
            if (is_curr) {
                ss << spinner[m_spinner_idx] << " ";
            } else {
                ss << "  ";
            }

            ss << "ID:" << f->id << " " << std::left << std::setw(8) << f->name;
            
            // Stack Usage calc
            size_t used = 0;
            if (f->stack_base) {
                uintptr_t base = (uintptr_t)f->stack_base;
                uintptr_t sp = (uintptr_t)f->stack_ptr;
                used = (base + f->stack_size) - sp;
            }
            ss << " STK:" << std::setw(4) << used << "B";
            
            // JITTER EFFECT: Randomized last byte of SP to look like activity
            uintptr_t visual_sp = (uintptr_t)f->stack_ptr;
            if (is_curr || (fast_rand() % 10) < 3) { // 30% chance to jitter
                 // Jitter the lower 8 bits
                 visual_sp = (visual_sp & ~0xFF) | (fast_rand() & 0xFF);
            }

            ss << " SP:" << std::hex << visual_sp;
            
            draw_text(rFibers.x + 1, rFibers.y + row, ss.str());
            row++;
            if (row >= rFibers.h - 1) break;
        }

        // JIT Stream (Below Ghost Map)
        Rect rJit = {1, gh + 3, gw, m_height - (gh + 3) - 2};
        draw_jit_stream(rJit, m_jit_cache);


        // Input Box (Bottom Right)
        Rect rInput = {gw + 3, m_height - 4, gw, 3};
        
        // Flash / Pulse Logic
        std::string boxColor = "";
        int timer = m_flash_timer.load();
        
        // If needing input, pulse the border
        bool requesting_input = m_input_text.empty();
        
        if (timer > 0) {
             m_flash_timer.fetch_sub(1);
             // Flash effect (solid border)
             for(int i=0;i<rInput.w; ++i) {
                int y_top = rInput.y * m_width + rInput.x + i;
                int y_bot = (rInput.y+rInput.h-1) * m_width + rInput.x + i;
                if(y_top < m_back_buffer.size()) m_back_buffer[y_top] = "="; 
                if(y_bot < m_back_buffer.size()) m_back_buffer[y_bot] = "=";
             }
        } else if (requesting_input) {
            // Pulse effect: Randomly change border style or just blink
            // Let's use a double line effect that flickers
            if ((fast_rand() % 20) == 0) {
                 // Blink gap
            } else {
                 draw_box(rInput); // Standard box
            }
        } else {
            draw_box(rInput);
        }
        
        draw_text(rInput.x + 2, rInput.y, " INPUT BUFFER ");
        if (!m_input_text.empty()) {
             draw_text(rInput.x + 2, rInput.y + 1, m_input_text.substr(0, rInput.w - 4));
        } else {
             // Prompt cursor
             if ((fast_rand() % 30) < 15) {
                draw_text(rInput.x + 2, rInput.y + 1, "_");
             }
        }

        present();
    }

    void SystemMonitor::clear_buffer() {
        // Reset to spaces
        for(auto& s : m_back_buffer) s = " ";
    }

    void SystemMonitor::draw_text(int x, int y, const std::string& text, const std::string&) {
        if (y < 0 || y >= m_height) return;
        
        for (size_t i = 0; i < text.size(); ++i) {
            int cx = x + i;
            if (cx >= 0 && cx < m_width) {
                 int idx = y * m_width + cx;
                 if(idx < m_back_buffer.size())
                    m_back_buffer[idx] = std::string(1, text[i]);
            }
        }
    }

    void SystemMonitor::draw_box(const Rect& rect) {
        // Horizontal
        for (int i = 0; i < rect.w; ++i) {
            int top_idx = (rect.y) * m_width + (rect.x + i);
            int bot_idx = (rect.y + rect.h - 1) * m_width + (rect.x + i);
            if (top_idx < m_back_buffer.size()) m_back_buffer[top_idx] = BoxChars::H_LINE;
            if (bot_idx < m_back_buffer.size()) m_back_buffer[bot_idx] = BoxChars::H_LINE;
        }
        // Vertical
        for (int i = 0; i < rect.h; ++i) {
            int left_idx = (rect.y + i) * m_width + rect.x;
            int right_idx = (rect.y + i) * m_width + (rect.x + rect.w - 1);
            if (left_idx < m_back_buffer.size()) m_back_buffer[left_idx] = BoxChars::V_LINE;
            if (right_idx < m_back_buffer.size()) m_back_buffer[right_idx] = BoxChars::V_LINE;
        }
        // Corners
        int tl = rect.y * m_width + rect.x;
        int tr = rect.y * m_width + (rect.x + rect.w - 1);
        int bl = (rect.y + rect.h - 1) * m_width + rect.x;
        int br = (rect.y + rect.h - 1) * m_width + (rect.x + rect.w - 1);
        
        if (tl < m_back_buffer.size()) m_back_buffer[tl] = BoxChars::TL;
        if (tr < m_back_buffer.size()) m_back_buffer[tr] = BoxChars::TR;
        if (bl < m_back_buffer.size()) m_back_buffer[bl] = BoxChars::BL;
        if (br < m_back_buffer.size()) m_back_buffer[br] = BoxChars::BR;
    }

    void SystemMonitor::draw_line(int x0, int y0, int x1, int y1) {
        int dx =  std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
        int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
        int err = dx + dy, e2; 
     
        while (true) {
            int idx = y0 * m_width + x0;
            if (idx >= 0 && idx < (int)m_back_buffer.size()) {
                m_back_buffer[idx] = "."; 
            }
            if (x0 == x1 && y0 == y1) break;
            e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    }

    void SystemMonitor::draw_ghost_map(const Rect& rect, const std::vector<uint8_t>&) {
        draw_box(rect);
        draw_text(rect.x + 2, rect.y, " GHOST MAP (1024TB) ");
        
        int inner_w = rect.w - 2;
        int inner_h = rect.h - 2;
        int max_cells = inner_w * inner_h;
        
        // 1. Update Heat Decay
        // Decrease heat
        for(auto& h : m_ghost_heat_map) {
            if (h > 0) h--; 
        }

        // 2. Active Updates (Simulation based on Resident Pages)
        // Since we don't have per-page monitoring in this TUI yet, we simulate "activity"
        // by lighting up random clusters if resident count > 0.
        // OR, we can just light up the first 'N' as hot if they 'changed'.
        // Better visual: "Scrubbing" effect.
        
        // Idle Scan: Randomly ping a block to see if it's there
        if ((fast_rand() % 100) < 10) { // 10% chance per frame to ping
             int rnd_idx = fast_rand() % m_ghost_heat_map.size();
             m_ghost_heat_map[rnd_idx] = 5; // Max Heat (Ping!)
        }
        
        // Also ensure resident pages are somewhat "lit"
        size_t resident = Hyperion::Core::MemoryManager::instance().get_resident_pages();
        for(size_t i=0; i<resident && i<m_ghost_heat_map.size(); ++i) {
             // Keep resident pages at a low simmer, pulsing occasionally
             if (m_ghost_heat_map[i] < 2) {
                 if((fast_rand() % 20) == 0) m_ghost_heat_map[i] = 4;
                 else m_ghost_heat_map[i] = 1; // minimum heat for resident
             }
        }

        // Render Map
        for (int i = 0; i < max_cells; ++i) {
            if (i >= m_ghost_heat_map.size()) break;

            int row = i / inner_w;
            int col = i % inner_w;
            int idx = (rect.y + 1 + row) * m_width + (rect.x + 1 + col);
            
            if (idx < m_back_buffer.size()) {
                int heat = m_ghost_heat_map[i];
                if (heat >= 5) m_back_buffer[idx] = BoxChars::HEAT_5;
                else if (heat == 4) m_back_buffer[idx] = BoxChars::HEAT_4;
                else if (heat == 3) m_back_buffer[idx] = BoxChars::HEAT_3;
                else if (heat == 2) m_back_buffer[idx] = BoxChars::HEAT_2;
                else m_back_buffer[idx] = " "; 
            }
        }
    }

    void SystemMonitor::draw_jit_stream(const Rect& rect, const std::vector<uint8_t>& code) {
         draw_box(rect);
         draw_text(rect.x + 2, rect.y, " JIT STREAM ");
         
         // Fill with scrolling data if empty
         // We can use a static offset to scroll
         static int scroll_offset = 0;
         scroll_offset++;

         int inner_w = rect.w - 2;
         int inner_h = rect.h - 2;
         
         static const char* dummy_asm[] = {
             "MOV  RAX, 0x0",
             "PUSH RBP",
             "MOV  RBP, RSP",
             "SUB  RSP, 0x40",
             "LEA  RDI, [RIP+0x20]",
             "CALL 0xFADE",
             "TEST RAX, RAX",
             "JZ   0x0040",
             "NOP",
             "PAUSE",
             "HLT"
         };
         int dummy_count = 11;
         
         for(int i=0; i<inner_h; ++i) {
             int line_idx = (scroll_offset / 10 + i) % dummy_count; // Scroll slowly
             std::string line = dummy_asm[line_idx];
             
             // Decorate with address
             std::stringstream ss;
             ss << "0x" << std::hex << std::setw(4) << std::setfill('0') << (0x1000 + line_idx * 4) << ": " << line;
             std::string full_line = ss.str();

             // Pad or cut
             if(full_line.size() > inner_w) full_line = full_line.substr(0, inner_w);
             
             draw_text(rect.x + 1, rect.y + 1 + i, full_line);
         }
    }

    void SystemMonitor::present() {
        std::string diff = compute_buffer_diff(m_front_buffer, m_back_buffer);
        if (!diff.empty()) {
            write(m_tty_fd, diff.c_str(), diff.size());
            m_front_buffer = m_back_buffer; // Copy strings. Expensive but correct.
        }
    }

    std::string SystemMonitor::compute_buffer_diff(const std::vector<std::string>& front, const std::vector<std::string>& back) {
        std::ostringstream ss;
        size_t size = front.size();
        
        for (size_t i = 0; i < size; ++i) {
            if (front[i] != back[i]) {
                int y = i / m_width;
                int x = i % m_width;
                ss << "\033[" << (y + 1) << ";" << (x + 1) << "H";
                ss << back[i];
            }
        }
        return ss.str();
    }
    
    // Updates
    void SystemMonitor::set_header_info(const std::string& info) { m_header_info = info; }
    void SystemMonitor::update_status_stats(const std::string& stats) { m_stats_info = stats; }
    void SystemMonitor::update_ghost_stats(size_t faults, size_t resident) { 
        m_page_faults = faults; m_resident_pages = resident; 
    }
    void SystemMonitor::update_simd_lanes(const float*) { } 
    void SystemMonitor::update_memory_view(const void* ptr, size_t size) {
        if (!ptr || size == 0) return;
        m_jit_cache.assign((const uint8_t*)ptr, (const uint8_t*)ptr + size);
    }
    void SystemMonitor::update_input_text(const std::string& text) { m_input_text = text; }
    void SystemMonitor::trigger_input_flash() { m_flash_timer.store(12); } 

}
