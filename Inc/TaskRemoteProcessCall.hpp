#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <span>

#include "Logger.hpp"
#include "RegistrationManager.hpp"
#include "Task.hpp"

#include "nunavut_assert.h"
#include "uavcan/node/ExecuteCommand_1_3.h"

struct RpcEntry
{
    using Callback = bool (*)(void*);

    std::uint16_t index;
    Callback      cb;
    void*         ctx;
};

// Zero-cast builders for member methods and free functions returning bool
template <auto Method, typename T>
constexpr RpcEntry make_rpc_entry(std::uint16_t index, T& obj)
{
    return RpcEntry{
        index,
        [](void* ctx) -> bool { return (static_cast<T*>(ctx)->*Method)(); },
        &obj
    };
}

template <bool (*Fn)()>
constexpr RpcEntry make_rpc_entry(std::uint16_t index)
{
    return RpcEntry{
        index,
        [](void*) -> bool { return Fn(); },
        nullptr
    };
}

template <typename... Adapters>
class TaskRemoteProcessCall : public TaskForServer<CyphalBuffer8, Adapters...>
{
public:
    enum class Status : std::uint8_t
    {
        SUCCESS        = uavcan_node_ExecuteCommand_Response_1_3_STATUS_SUCCESS,
        FAILURE        = uavcan_node_ExecuteCommand_Response_1_3_STATUS_FAILURE,
        NOT_AUTHORIZED = uavcan_node_ExecuteCommand_Response_1_3_STATUS_NOT_AUTHORIZED,
        BAD_COMMAND    = uavcan_node_ExecuteCommand_Response_1_3_STATUS_BAD_COMMAND,
        BAD_PARAMETER  = uavcan_node_ExecuteCommand_Response_1_3_STATUS_BAD_PARAMETER,
        BAD_STATE      = uavcan_node_ExecuteCommand_Response_1_3_STATUS_BAD_STATE,
        INTERNAL_ERROR = uavcan_node_ExecuteCommand_Response_1_3_STATUS_INTERNAL_ERROR,
    };

    TaskRemoteProcessCall() = delete;
    TaskRemoteProcessCall(std::span<const RpcEntry> entries,
                          std::uint32_t interval,
                          std::uint32_t tick,
                          std::tuple<Adapters...>& adapters)
        : TaskForServer<CyphalBuffer8, Adapters...>(interval, tick, adapters),
          entries_(entries) {}

    void registerTask(RegistrationManager* manager, std::shared_ptr<Task> task) override
    {
        manager->server(uavcan_node_ExecuteCommand_1_3_FIXED_PORT_ID_, task);
    }

    void unregisterTask(RegistrationManager* manager, std::shared_ptr<Task> task) override
    {
        manager->unserver(uavcan_node_ExecuteCommand_1_3_FIXED_PORT_ID_, task);
    }

    void handleTaskImpl() override
    {
        if (TaskForServer<CyphalBuffer8, Adapters...>::buffer_.is_empty())
        {
            log(LOG_LEVEL_TRACE, "TaskRemoteProcessCall: empty buffer\r\n");
            return;
        }

        log(LOG_LEVEL_INFO, "TaskRemoteProcessCall: received request\r\n");
        const std::size_t count = TaskForServer<CyphalBuffer8, Adapters...>::buffer_.size();
        for (std::size_t i = 0; i < count; ++i)
        {
            std::shared_ptr<CyphalTransfer> transfer = TaskForServer<CyphalBuffer8, Adapters...>::buffer_.pop();
            if (transfer->metadata.transfer_kind != CyphalTransferKindRequest)
            {
                log(LOG_LEVEL_ERROR, "TaskRemoteProcessCall Error: %4d %4d\r\n",
                    transfer->metadata.remote_node_id,
                    transfer->metadata.transfer_kind);
                continue;
            }

            uavcan_node_ExecuteCommand_Request_1_3 request{};
            std::size_t request_size = transfer->payload_size;
            const std::int8_t deser_res = uavcan_node_ExecuteCommand_Request_1_3_deserialize_(
                &request,
                static_cast<const std::uint8_t*>(transfer->payload),
                &request_size);
            if (deser_res < 0)
            {
                log(LOG_LEVEL_ERROR, "TaskRemoteProcessCall: deserialize failed (%d)\r\n", deser_res);
                continue;
            }

            uavcan_node_ExecuteCommand_Response_1_3 data{};
            data.status = static_cast<std::uint8_t>(call(request.command));

            constexpr std::size_t PAYLOAD_SIZE = uavcan_node_ExecuteCommand_Response_1_3_SERIALIZATION_BUFFER_SIZE_BYTES_;
            std::uint8_t payload[PAYLOAD_SIZE]{};

            TaskForServer<CyphalBuffer8, Adapters...>::publish(
                PAYLOAD_SIZE,
                payload,
                &data,
                reinterpret_cast<std::int8_t (*)(const void* const, std::uint8_t* const, std::size_t* const)>(
                    uavcan_node_ExecuteCommand_Response_1_3_serialize_),
                uavcan_node_ExecuteCommand_1_3_FIXED_PORT_ID_,
                transfer->metadata.remote_node_id,
                transfer->metadata.transfer_id);

            log(LOG_LEVEL_DEBUG, "TaskRemoteProcessCall::handleTaskImpl: shared counter %4d\r\n",
                transfer.use_count());
        }
    }

    Status call(std::uint16_t index) const
    {
        for (const auto& entry : entries_)
        {
            if (entry.index == index)
            {
                if (!entry.cb)
                    return Status::BAD_COMMAND;
                return entry.cb(entry.ctx) ? Status::SUCCESS : Status::FAILURE;
            }
        }
        return Status::BAD_COMMAND;
    }

    std::span<const RpcEntry> entries() const { return entries_; }

private:
    std::span<const RpcEntry> entries_;
};