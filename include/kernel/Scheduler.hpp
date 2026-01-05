#pragma once

#include <cstdint>
#include <vector>
#include <functional>
#include <string>
#include <unistd.h> // size_t

extern "C" {
    void switch_context(void** old_sp, void* new_sp);
    void verify_cpu_features();
}

namespace Kernel {

struct Fiber {
    uint64_t id;
    void* stack_ptr; // Current SP
    void* stack_base; // Mmap base
    size_t stack_size;
    std::string name;
    bool is_completed;

    Fiber(uint64_t id, std::string name, size_t stack_size);
    ~Fiber();
};

class Scheduler {
public:
    static Scheduler& Get();

    void Init();
    void Spawn(std::string name, std::function<void()> entry);
    void Yield();
    void Run();

    Fiber* Current() { return current_fiber; }
    const std::vector<Fiber*>& AllFibers() { return fibers; }

private:
    Scheduler() = default;

    static void Trampoline();

    std::vector<Fiber*> fibers;
    Fiber* current_fiber = nullptr;
    Fiber* main_fiber = nullptr; // The OS thread we started on
    size_t current_idx = 0;
};

} // namespace Kernel
