#ifndef _TASKRESPONDREGISTERACCESS_HPP_
#define _TASKRESPONDREGISTERACCESS_HPP_

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

#include "Task.hpp"
#include "RegistrationManager.hpp"
#include "RecordRegistry.hpp"
#include "Register.hpp"
#include "RecordStore.hpp"
#include "Logger.hpp"

#include "nunavut_assert.h"
#include "uavcan/_register/Access_1_0.h"

template <typename Registry, typename Store, typename... Adapters>
class TaskRespondRegisterAccess : public TaskForServer<CyphalBuffer8, Adapters...>
{
public:
    TaskRespondRegisterAccess() = delete;
    TaskRespondRegisterAccess(Registry& registry, Store& store, uint32_t interval, uint32_t tick, std::tuple<Adapters...>& adapters)
        : TaskForServer<CyphalBuffer8, Adapters...>(interval, tick, adapters), registry_(registry), store_(store) {}

    virtual void registerTask(RegistrationManager* manager, std::shared_ptr<Task> task) override;
    virtual void unregisterTask(RegistrationManager* manager, std::shared_ptr<Task> task) override;
    virtual void handleTaskImpl() override;

private:
    static constexpr std::size_t max_payload_size()
    {
        std::size_t max_sz = 1;
        for (const auto& reg : Registry::registers)
            max_sz = std::max(max_sz, reg.entry->payload_size);
        return max_sz;
    }

    void log_register_value(std::string_view name, const uavcan_register_Value_1_0& val);

protected:
    Registry& registry_;
    Store&    store_;
};

template <typename Registry, typename Store, typename... Adapters>
void TaskRespondRegisterAccess<Registry, Store, Adapters...>::handleTaskImpl()
{
    if (TaskForServer<CyphalBuffer8, Adapters...>::buffer_.is_empty())
    {
        log(LOG_LEVEL_TRACE, "TaskRespondRegisterAccess: empty buffer\r\n");
        return;
    }

    log(LOG_LEVEL_INFO, "TaskRespondRegisterAccess: received request\r\n");
    const size_t count = TaskForServer<CyphalBuffer8, Adapters...>::buffer_.size();
    for (size_t i = 0; i < count; ++i)
    {
        std::shared_ptr<CyphalTransfer> transfer = TaskForServer<CyphalBuffer8, Adapters...>::buffer_.pop();
        if (transfer->metadata.transfer_kind != CyphalTransferKindRequest)
        {
            log(LOG_LEVEL_ERROR, "TaskRespondRegisterAccess Error: %4d %4d\r\n",
                transfer->metadata.remote_node_id,
                transfer->metadata.transfer_kind);
            continue;
        }

        uavcan_register_Access_Request_1_0 request{};
        size_t request_size = transfer->payload_size;
        const int8_t deser_res = uavcan_register_Access_Request_1_0_deserialize_(
            &request,
            static_cast<const uint8_t*>(transfer->payload),
            &request_size);
        if (deser_res < 0)
        {
            log(LOG_LEVEL_ERROR, "TaskRespondRegisterAccess: deserialize failed (%d)\r\n", deser_res);
            continue;
        }

        const size_t name_len = std::min(request.name.name.count, sizeof(request.name.name.elements));
        const std::string_view name(reinterpret_cast<const char*>(request.name.name.elements), name_len);

        log(LOG_LEVEL_INFO, "TaskRespondRegisterAccess: %4d %4d %.*s\r\n", transfer->metadata.remote_node_id, transfer->metadata.transfer_kind,
        		static_cast<int>(name.size()), name.data());

        uavcan_register_Access_Response_1_0 data{};
        const auto* reg = registry_.lookup(name);


        if (!reg)
        {
        	log(LOG_LEVEL_ERROR, "TaskRespondRegisterAccess: lookup error %.*s\r\n", static_cast<int>(name.size()), name.data());
        	// Unknown register
            uavcan_register_Value_1_0_select_empty_(&data.value);
            data._mutable   = false;
            data.persistent = false;
        }
        else
        {
            alignas(std::max_align_t) std::array<std::uint8_t, max_payload_size()> scratch{};

            // Set if request.value is non-empty
            if (!uavcan_register_Value_1_0_is_empty_(&request.value))
            {
                if (reg->decode_raw(request.value, scratch.data()))
                {
                    if (!store_.set_raw(*reg->entry, scratch.data()))
                    {
                        log(LOG_LEVEL_ERROR, "TaskRespondRegisterAccess: set_raw failed\r\n");
                    }
                }
                else
                {
                    log(LOG_LEVEL_ERROR, "TaskRespondRegisterAccess: decode failed\r\n");
                }
            }

            // Always respond with the current stored value (for both Get and Set)
            data._mutable   = true;
            data.persistent = true;

            if (!store_.get_raw(*reg->entry, scratch.data()) ||
                !reg->encode_raw(scratch.data(), data.value))
            {
                uavcan_register_Value_1_0_select_empty_(&data.value);
            }
        }

        log_register_value(name, data.value);

        constexpr size_t PAYLOAD_SIZE = uavcan_register_Access_Response_1_0_SERIALIZATION_BUFFER_SIZE_BYTES_;
        uint8_t payload[PAYLOAD_SIZE]{};

        TaskForServer<CyphalBuffer8, Adapters...>::publish(
            PAYLOAD_SIZE,
            payload,
            &data,
            reinterpret_cast<int8_t (*)(const void* const, uint8_t* const, size_t* const)>(
                uavcan_register_Access_Response_1_0_serialize_),
            uavcan_register_Access_1_0_FIXED_PORT_ID_,
            transfer->metadata.remote_node_id,
            transfer->metadata.transfer_id);

        log(LOG_LEVEL_DEBUG, "TaskRespondRegisterAccess::handleTaskImpl: shared counter %4d\r\n",
            transfer.use_count());
    }
}

template <typename Registry, typename Store, typename... Adapters>
void TaskRespondRegisterAccess<Registry, Store, Adapters...>::registerTask(RegistrationManager* manager,
                                                                           std::shared_ptr<Task> task)
{
    manager->server(uavcan_register_Access_1_0_FIXED_PORT_ID_, task);
}

template <typename Registry, typename Store, typename... Adapters>
void TaskRespondRegisterAccess<Registry, Store, Adapters...>::unregisterTask(RegistrationManager* manager,
                                                                             std::shared_ptr<Task> task)
{
    manager->unserver(uavcan_register_Access_1_0_FIXED_PORT_ID_, task);
}

template <typename Registry, typename Store, typename... Adapters>
void TaskRespondRegisterAccess<Registry, Store, Adapters...>::log_register_value(std::string_view name, 
                                                                                  const uavcan_register_Value_1_0& val)
{
    if (uavcan_register_Value_1_0_is_empty_(&val))
    {
        log(LOG_LEVEL_INFO, "Reg[%.*s] = <empty>\r\n", static_cast<int>(name.size()), name.data());
    }
    else if (uavcan_register_Value_1_0_is_natural32_(&val) && val.natural32.value.count > 0)
    {
        log(LOG_LEVEL_INFO, "Reg[%.*s] = u32:%lu (count=%u)\r\n",
            static_cast<int>(name.size()), name.data(),
            static_cast<unsigned long>(val.natural32.value.elements[0]),
            static_cast<unsigned>(val.natural32.value.count));
    }
    else if (uavcan_register_Value_1_0_is_natural64_(&val) && val.natural64.value.count > 0)
    {
        log(LOG_LEVEL_INFO, "Reg[%.*s] = u64:%llu (count=%u)\r\n",
            static_cast<int>(name.size()), name.data(),
            static_cast<unsigned long long>(val.natural64.value.elements[0]),
            static_cast<unsigned>(val.natural64.value.count));
    }
    else if (uavcan_register_Value_1_0_is_real32_(&val) && val.real32.value.count > 0)
    {
        // Note: %f requires -u _printf_float with nano.specs; %d.%03d avoids it
        const float f = val.real32.value.elements[0];
        const int whole = static_cast<int>(f);
        const int frac  = static_cast<int>((f - static_cast<float>(whole)) * 1000.0f);
        log(LOG_LEVEL_INFO, "Reg[%.*s] = f32:%d.%03d\r\n",
            static_cast<int>(name.size()), name.data(),
            whole, frac < 0 ? -frac : frac);
    }
    else if (uavcan_register_Value_1_0_is_unstructured_(&val))
    {
        log(LOG_LEVEL_INFO, "Reg[%.*s] = unstructured (%u bytes)\r\n",
            static_cast<int>(name.size()), name.data(),
            static_cast<unsigned>(val.unstructured.value.count));
    }
}

#endif /* _TASKRESPONDREGISTERACCESS_HPP_ */
