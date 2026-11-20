#pragma once

#include <cstdint>
#include <utility>

#include "HeapAllocation.hpp"
#include "RegistrationManager.hpp"
#include "Task.hpp"
#include "TaskPeriodic.hpp"

template <typename T, typename... Args>
inline void register_task_with_heap(RegistrationManager& rm, Args&&... args)
{
    static SafeAllocator<T, LocalHeap> alloc;
    rm.add(alloc_unique_custom<T>(alloc, std::forward<Args>(args)...));
}

// Bind any object + member function returning bool: obj.method()
template <auto Method, typename T>
inline void register_periodic(RegistrationManager& rm, T& obj, uint32_t interval, uint32_t tick)
{
    register_task_with_heap<TaskPeriodic>(
        rm,
        [](void* ctx) -> bool { return (static_cast<T*>(ctx)->*Method)(); },
        &obj,
        interval,
        tick);
}

// Overload for free functions returning bool: fn()
template <bool (*Fn)()>
inline void register_periodic(RegistrationManager& rm, uint32_t interval, uint32_t tick)
{
    register_task_with_heap<TaskPeriodic>(
        rm,
        [](void*) -> bool { return Fn(); },
        nullptr,
        interval,
        tick);
}
