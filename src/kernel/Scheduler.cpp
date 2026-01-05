#include "kernel/Scheduler.hpp"
#include <sys/mman.h>
#include <iostream>
#include <cassert>
#include <cstring>

namespace Kernel {

// Forward declaration of the trampoline entry
void S_Entry();

// Global pointer for the trampoline to find the function to run
static std::function<void()>* g_next_task = nullptr;

Fiber::Fiber(uint64_t id, std::string name, size_t stack_size) 
    : id(id), name(name), stack_size(stack_size), is_completed(false) {

    // If stack_size is 0, it means this is the Main Thread wrapper. No allocation.
    if (stack_size == 0) {
        stack_base = nullptr;
        stack_ptr = nullptr;
        return;
    }

    // Allocate stack with mmap
    stack_base = mmap(nullptr, stack_size, PROT_READ | PROT_WRITE, 
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (stack_base == MAP_FAILED) {
        perror("mmap failed");
        exit(1);
    }
    
    // Stack grows down. Initial SP is at the end of the block.
    // Align to 16 bytes.
    uintptr_t sp_addr = (uintptr_t)stack_base + stack_size;
    sp_addr &= ~0xF; 
    stack_ptr = (void*)sp_addr;
}

Fiber::~Fiber() {
    if (stack_base) {
        munmap(stack_base, stack_size);
    }
}

Scheduler& Scheduler::Get() {
    static Scheduler instance;
    return instance;
}

void Scheduler::Init() {
    verify_cpu_features();
    
    // ARCHITECTURAL NOTE:
    // The main thread is implicitly converted into fiber 0.
    // We do not allocate a stack for it; we simply capture its state during the first switch.
    main_fiber = new Fiber(0, "Main", 0);
    main_fiber->stack_base = nullptr; 
    current_fiber = main_fiber;
    fibers.push_back(main_fiber);
}

void Scheduler::Trampoline() {
    // CRITICAL:
    // This function acts as the "Event Horizon" for new fibers.
    // It is never called directly; execution jumps here via the 'ret' instruction in switch_context
    // when a new fiber stack is first popped.
    
    // In a production kernel, we would pop the task pointer from a specific register or stack slot.
    // However, due to the Unikernel flat model, we can rely on register preservation.
}

// Helper to push values to the stack pointer
template<typename T>
void Push(void*& sp, T val) {
    uintptr_t addr = (uintptr_t)sp;
    addr -= sizeof(T);
    *(T*)addr = val;
    sp = (void*)addr;
}

void Scheduler::Spawn(std::string name, std::function<void()> entry) {
    Fiber* f = new Fiber(fibers.size(), name, 1024 * 1024); // 1MB Stack
    
    // Forge Stack
    void* sp = f->stack_ptr;

    // Use a wrapper that can be passed as a function pointer or similar.
    // Since we can't easily pass the std::function through registers in raw ASM without ABI code,
    // we'll make a specialized static function that invokes a stored global/map.
    // Or we simply use a raw stateless lambda mechanism.
    
    // Check for null entry point
    if (!entry) {
        std::cerr << "PANIC: Spawned null fiber" << std::endl;
        exit(1);
    }
    
    // OPTIMIZATION:
    // We allocate the std::function on the heap and pass it via a Callee-Saved Register (X19/R12).
    // This allows the Trampoline to retrieve the invocable without stack pointer manipulation complexities.
    auto* task_ptr = new std::function<void()>(entry);
    
    // Static Thunk to bridge C-style function pointer to C++ Lambda
    auto thunk = [](void* arg) {
        auto* func = (std::function<void()>*)arg;
        (*func)();
    };
    
    // Stack Layout Fabrication for Context Switch:
    // We mimic the stack frame that 'switch_context' expects to see when it restores a fiber.
    
    #if defined(__x86_64__)
        // x86_64 ABI:
        // R12 is Callee-Saved. We use it to hold the task_ptr.
        
        Push<void*>(sp, (void*)S_Entry); // Ret Addr implies: "After Restore, RET to S_Entry"
        
        // Push Callee-Saved registers (Val=0, mostly)
        Push<uint64_t>(sp, 0); // RBX
        Push<uint64_t>(sp, 0); // RBP
        Push<void*>(sp, task_ptr); // R12 (ARGUMENT HOLDER)
        Push<uint64_t>(sp, 0); // R13
        Push<uint64_t>(sp, 0); // R14
        Push<uint64_t>(sp, 0); // R15
        
    #elif defined(__aarch64__)
        // ARM64 switch_context:
        // LDP X29, X30 (FP, LR)
        // ...
        // LDP X19, X20
        
        // Stack layout needs to match LDP xA, xB which loads [SP] -> xA, [SP+8] -> xB.
        // Since Push grows down, the LAST pushed item is at [SP].
        // So we must push xB (High) then xA (Low).
        
        // Pairs: x19/x20, x21/x22, ... x29/x30
        
        Push<uint64_t>(sp, 0);     // X20
        Push<void*>(sp, task_ptr); // X19 (Holds task_ptr)
        
        Push<uint64_t>(sp, 0);     // X22
        Push<uint64_t>(sp, 0);     // X21
        
        Push<uint64_t>(sp, 0);     // X24
        Push<uint64_t>(sp, 0);     // X23
        
        Push<uint64_t>(sp, 0);     // X26
        Push<uint64_t>(sp, 0);     // X25
        
        Push<uint64_t>(sp, 0);     // X28
        Push<uint64_t>(sp, 0);     // X27
        
        Push<void*>(sp, (void*)S_Entry); // X30 (LR must hold Entry)
        Push<uint64_t>(sp, 0);     // X29 (FP)
        
    #else
        #error "Unsupported Architecture"
    #endif

    f->stack_ptr = sp;
    fibers.push_back(f);
}

// Static Entry Point
void S_Entry() {
    // We are now invalidating the "return address" logic because we never return from here ideally,
    // or if we do, we need to handle it.
    
    // Retrieve the function pointer from the register we saved it in.
    std::function<void()>* func = nullptr;
    
    #if defined(__x86_64__)
        // We put it in R12
        asm volatile("mov %%r12, %0" : "=r"(func));
    #elif defined(__aarch64__)
        // We put it in X19
        asm volatile("mov %0, x19" : "=r"(func));
    #endif
    
    if (func) {
        (*func)();
        delete func;
    }
    
    // Fiber Finished. Loop forever or yield.
    while(true) {
        Scheduler::Get().Current()->is_completed = true;
        Scheduler::Get().Yield();
    }
}

void Scheduler::Yield() {
    Fiber* prev = current_fiber;
    
    // Round Robin
    current_idx = (current_idx + 1) % fibers.size();
    current_fiber = fibers[current_idx];
    
    if (prev == current_fiber) return;
    
    switch_context(&prev->stack_ptr, current_fiber->stack_ptr);
}

void Scheduler::Run() {
    // Just yield loop
    while (true) {
        Yield();
        // Sleep a bit?
        usleep(1000); 
    }
}

} // namespace Kernel

// Need to define S_Entry so it can be taken address of
namespace Kernel { void S_Entry(); }
