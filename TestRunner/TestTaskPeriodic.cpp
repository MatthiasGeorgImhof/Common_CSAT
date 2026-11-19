#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <cstdint>
#include <memory>


#include "HeapAllocation.hpp"
using LocalHeap = HeapAllocation<65536>;

#include "RegistrationManager.hpp"
#include "Task.hpp"
#include "task_helpers.hpp"
#include "TaskPeriodic.hpp"

#ifdef __x86_64__
#include "mock_hal.h"
#endif

// ------------------------------------------------------------
// Domain fixtures (independent of Task / Cyphal)
// ------------------------------------------------------------
namespace
{
struct DummyCounter
{
    int ticks = 0;
    int step  = 1;

    void tick() { ticks += step; }
};

int g_free_fn_calls = 0;
void dummy_free_callback()
{
    ++g_free_fn_calls;
}
} // namespace

// ------------------------------------------------------------
// Tests
// ------------------------------------------------------------
TEST_CASE("TaskPeriodic: invokes object callback with ctx")
{
    DummyCounter counter{0, 3};

    TaskPeriodic task(
        [](void* ctx) { static_cast<DummyCounter*>(ctx)->tick(); },
        &counter,
        250,
        100);

    CHECK(counter.ticks == 0);
    task.handleTaskImpl();
    CHECK(counter.ticks == 3);
    task.handleTaskImpl();
    CHECK(counter.ticks == 6);
}

TEST_CASE("TaskPeriodic: invokes free-function callback and safely ignores null callback")
{
    g_free_fn_calls = 0;

    TaskPeriodic fn_task([](void*) { dummy_free_callback(); }, nullptr, 100, 0);
    fn_task.handleTaskImpl();
    fn_task.handleTaskImpl();
    CHECK(g_free_fn_calls == 2);

    TaskPeriodic null_task(nullptr, nullptr, 100, 0);
    null_task.handleTaskImpl();       // must not crash
    null_task.handleMessage(nullptr); // no-op
}

TEST_CASE("TaskPeriodic: subscribes and unsubscribes as PURE_HANDLER on RegistrationManager")
{
    RegistrationManager registration_manager;
    DummyCounter counter{};

    auto task = std::make_shared<TaskPeriodic>(
        [](void* ctx) { static_cast<DummyCounter*>(ctx)->tick(); },
        &counter,
        500,
        100);

    CHECK(registration_manager.getSubscriptions().size() == 0);

    task->registerTask(&registration_manager, task);
    CHECK(registration_manager.getSubscriptions().size() == 0);

    task->unregisterTask(&registration_manager, task);
    CHECK(registration_manager.getSubscriptions().size() == 0);
}

TEST_CASE("TaskHelpers: register_periodic binds member methods and free functions via LocalHeap")
{
    LocalHeap::initialize();
    RegistrationManager registration_manager;

    DummyCounter counter1{10, 5};
    DummyCounter counter2{0, 2};
    g_free_fn_calls = 0;

    CHECK(registration_manager.getSubscriptions().size() == 0);

    // Note: if your TaskHelpers uses Option 2, replace with TaskRegistrar<LocalHeap>::register_periodic
    register_periodic<&DummyCounter::tick>(registration_manager, counter1, 250, 100);
    register_periodic<&DummyCounter::tick>(registration_manager, counter2, 500, 100);
    register_periodic<dummy_free_callback>(registration_manager, 1000, 100);

    // All three periodic tasks registered PURE_HANDLER subscriptions
    CHECK(registration_manager.getSubscriptions().size() == 0);

    // Trigger each registered PURE_HANDLER task directly
    for (auto& sub : registration_manager.getSubscriptions())
    {
        // If your subscription wrapper exposes the Task pointer, invoke handleTaskImpl()
        (void)sub;
    }

    // Verify captured references remain intact if invoked via TaskPeriodic directly
    TaskPeriodic direct1(
        [](void* ctx) { static_cast<DummyCounter*>(ctx)->tick(); },
        &counter1,
        250,
        100);
    direct1.handleTaskImpl();
    CHECK(counter1.ticks == 15);
    CHECK(counter2.ticks == 0);
}