#ifndef INC_TASKRESPONDREGISTRYLIST_HPP_
#define INC_TASKRESPONDREGISTRYLIST_HPP_

#include <algorithm>
#include <cstring>
#include <string_view>

#include "Task.hpp"
#include "RegistrationManager.hpp"
#include "Mram.hpp"
#include "Logger.hpp"

#include "nunavut_assert.h"
#include "uavcan/_register/List_1_0.h"

template <typename Registry, typename... Adapters>
class TaskRespondRegisterList : public TaskForServer<CyphalBuffer8, Adapters...>
{
public:
    TaskRespondRegisterList() = delete;
    TaskRespondRegisterList(Registry& registry,
                            uint32_t interval,
                            uint32_t tick,
                            std::tuple<Adapters...>& adapters)
        : TaskForServer<CyphalBuffer8, Adapters...>(interval, tick, adapters),
          registry_(registry) {}

    virtual void registerTask(RegistrationManager* manager, std::shared_ptr<Task> task) override;
    virtual void unregisterTask(RegistrationManager* manager, std::shared_ptr<Task> task) override;
    virtual void handleTaskImpl() override;

private:
    constexpr std::string_view name_at(std::size_t index) const
    {
        const auto* entry = registry_.lookup_by_index(index);
        if (!entry)
            return {};

        if constexpr (requires { entry->mram->name; })
            return std::string_view(entry->mram->name);
        else
            return std::string_view(entry->name);
    }

protected:
    Registry& registry_;
};

template <typename Registry, typename... Adapters>
void TaskRespondRegisterList<Registry, Adapters...>::handleTaskImpl()
{
    if (TaskForServer<CyphalBuffer8, Adapters...>::buffer_.is_empty())
    {
        log(LOG_LEVEL_TRACE, "TaskRespondRegisterList: empty buffer\r\n");
        return;
    }

    log(LOG_LEVEL_INFO, "TaskRespondRegisterList: received request\r\n");
    const size_t count = TaskForServer<CyphalBuffer8, Adapters...>::buffer_.size();
    for (size_t i = 0; i < count; ++i)
    {
        std::shared_ptr<CyphalTransfer> transfer = TaskForServer<CyphalBuffer8, Adapters...>::buffer_.pop();
        if (transfer->metadata.transfer_kind != CyphalTransferKindRequest)
        {
            log(LOG_LEVEL_ERROR, "TaskRespondRegisterList Error: %4d %4d\r\n",
                transfer->metadata.remote_node_id,
                transfer->metadata.transfer_kind);
            continue;
        }

        uavcan_register_List_Request_1_0 request{};
        size_t request_size = transfer->payload_size;
        const int8_t deser_res = uavcan_register_List_Request_1_0_deserialize_(
            &request,
            static_cast<const uint8_t*>(transfer->payload),
            &request_size);
        if (deser_res < 0)
        {
            log(LOG_LEVEL_ERROR, "TaskRespondRegisterList: deserialize failed (%d)\r\n", deser_res);
            continue;
        }

        const std::string_view name = name_at(request.index);

        log(LOG_LEVEL_INFO, "TaskRespondRegisterList: %4d %4d\r\n",
            transfer->metadata.remote_node_id,
            transfer->metadata.transfer_kind);

        uavcan_register_List_Response_1_0 data{};
        const size_t copy_len = std::min(name.size(), sizeof(data.name.name.elements));
        data.name.name.count = copy_len;
        if (copy_len > 0)
            std::memcpy(data.name.name.elements, name.data(), copy_len);

        constexpr size_t PAYLOAD_SIZE = uavcan_register_List_Response_1_0_SERIALIZATION_BUFFER_SIZE_BYTES_;
        uint8_t payload[PAYLOAD_SIZE]{};

        TaskForServer<CyphalBuffer8, Adapters...>::publish(
            PAYLOAD_SIZE,
            payload,
            &data,
            reinterpret_cast<int8_t (*)(const void* const, uint8_t* const, size_t* const)>(
                uavcan_register_List_Response_1_0_serialize_),
            uavcan_register_List_1_0_FIXED_PORT_ID_,
            transfer->metadata.remote_node_id,
            transfer->metadata.transfer_id);

        log(LOG_LEVEL_DEBUG, "TaskRespondRegisterList::handleTaskImpl: shared counter %4d\r\n", transfer.use_count());
    }
}

template <typename Registry, typename... Adapters>
void TaskRespondRegisterList<Registry, Adapters...>::registerTask(RegistrationManager* manager,
                                                                  std::shared_ptr<Task> task)
{
    manager->server(uavcan_register_List_1_0_FIXED_PORT_ID_, task);
}

template <typename Registry, typename... Adapters>
void TaskRespondRegisterList<Registry, Adapters...>::unregisterTask(RegistrationManager* manager,
                                                                    std::shared_ptr<Task> task)
{
    manager->unserver(uavcan_register_List_1_0_FIXED_PORT_ID_, task);
}

#endif /* INC_TASKRESPONDREGISTRYLIST_HPP_ */