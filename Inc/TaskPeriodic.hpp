// TaskPeriodic.hpp
#pragma once

#include <cstdint>
#include <memory>
#include "Task.hpp"
#include "RegistrationManager.hpp"

class TaskPeriodic : public Task
{
public:
    using Callback = void (*)(void*);

    TaskPeriodic() = delete;
    TaskPeriodic(Callback cb, void* ctx, uint32_t interval, uint32_t tick)
        : Task(interval, tick), cb_(cb), ctx_(ctx) {}

    void handleMessage(std::shared_ptr<CyphalTransfer>) override {}

    void registerTask(RegistrationManager* manager, std::shared_ptr<Task> task) override
    {
        manager->subscribe(PURE_HANDLER, task);
    }

    void unregisterTask(RegistrationManager* manager, std::shared_ptr<Task> task) override
    {
        manager->unsubscribe(PURE_HANDLER, task);
    }

    void handleTaskImpl() override
    {
        if (cb_)
            cb_(ctx_);
    }

private:
    Callback cb_;
    void*    ctx_;
};

