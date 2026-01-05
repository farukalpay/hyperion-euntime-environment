#include "../include/core/ProcessingUnit.hpp"
#include "core/InputIngest.hpp"
#include "monitor/SystemMonitor.hpp"
#include "kernel/Scheduler.hpp"
#include "mm/MemoryManager.hpp"
#include <csignal>
#include <iostream>
#include <clocale>
#include <poll.h>
#include <unistd.h>

// Global context for Fibers
Hyperion::ProcessingUnit* g_runtime = nullptr;
bool g_running = true;

// Graceful Exit
void signal_handler(int) {
    g_running = false;
    Hyperion::TUI::SystemMonitor::instance().shutdown();
    exit(0);
}

// Visual & Logic Fiber
// Runs at 60Hz ideally, managing Input polling and Rendering
void UI_Fiber_Func() {
    auto& retina = Hyperion::TUI::SystemMonitor::instance();
    struct pollfd pfd = { STDIN_FILENO, POLLIN, 0 };

    while (g_running) {
        // Non-blocking Input Check
        int ret = poll(&pfd, 1, 0); 
        if (ret > 0 && (pfd.revents & POLLIN)) {
            char c;
            if (read(STDIN_FILENO, &c, 1) > 0) {
                if (c == 'q') g_running = false;
            }
        }
        
        // Orchestrate core logic
        if (g_runtime) g_runtime->Update();
        
        // Render
        retina.render();
        
        // Yield to let other fibers (InputIngest, Workers) run
        Kernel::Scheduler::Get().Yield();
    }
}

// Data Ingestion Fiber
// Checks system clipboard for new text
void InputIngest_Fiber_Func() {
    while (g_running) {
        if (auto clip = Hyperion::Core::InputIngest::check()) {
            if (g_runtime) g_runtime->Ingest(*clip);
        }
        // Lower priority yield
        Kernel::Scheduler::Get().Yield();
    }
}

int main(int argc, char* argv[]) {
    // 1. Environment Setup
    std::setlocale(LC_ALL, "en_US.UTF-8");
    std::ios::sync_with_stdio(false);
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // 2. Kernel Initialization
    Kernel::Scheduler::Get().Init();

    // 3. Ghost Memory Boot (Explicit check before Engine start)
    auto& ghost = Hyperion::Core::MemoryManager::instance();
    if (!ghost.initialize()) {
        std::cerr << "FATAL: Ghost Memory init failed." << std::endl;
        return 1;
    }
    ghost.run_self_test();

    // 4. Processing Unit & System Monitor Launch
    Hyperion::ProcessingUnit runtime(argc, argv);
    g_runtime = &runtime;

    if (!Hyperion::TUI::SystemMonitor::instance().initialize()) {
        std::cerr << "FATAL: TUI init failed." << std::endl;
        return 1;
    }

    // 5. Spawn Fibers
    Kernel::Scheduler::Get().Spawn("UI_Fiber", UI_Fiber_Func);
    Kernel::Scheduler::Get().Spawn("Clip_Fib", InputIngest_Fiber_Func);

    // 6. Enter Unikernel Loop
    runtime.Start();
    Kernel::Scheduler::Get().Run(); // Never returns until exit

    return 0; 
}
