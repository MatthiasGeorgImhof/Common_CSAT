#ifndef __TASKRESPONDWRITE_HPP_
#define __TASKRESPONDWRITE_HPP_

#include "Task.hpp"
#include "RegistrationManager.hpp"
#include "InputOutputStream.hpp"

#include "nunavut_assert.h"
#include "uavcan/file/Write_1_1.h"
#include "uavcan/file/Error_1_0.h"

#include <cstring>

template <typename Heap, OutputStreamConcept Stream, typename... Adapters>
class TaskRespondWrite : public TaskForServer<CyphalBuffer8, Adapters...>
{
public:
    TaskRespondWrite() = delete;

    TaskRespondWrite(Stream& stream,
                     uint32_t interval,
                     uint32_t tick,
                     std::tuple<Adapters...>& adapters)
        : TaskForServer<CyphalBuffer8, Adapters...>(interval, tick, adapters),
          stream_(stream),
          active_write_(false)
    {}

    void registerTask(RegistrationManager* manager, std::shared_ptr<Task> task) override
    {
        manager->server(uavcan_file_Write_1_1_FIXED_PORT_ID_, task);
    }

    void unregisterTask(RegistrationManager* manager, std::shared_ptr<Task> task) override
    {
        manager->unserver(uavcan_file_Write_1_1_FIXED_PORT_ID_, task);
    }

    void handleTaskImpl() override
    {
        if (this->buffer_.is_empty())
            return;

        auto transfer = this->buffer_.pop();
        if (transfer->metadata.transfer_kind != CyphalTransferKindRequest)
            return;

        uavcan_file_Write_Request_1_1 req{};
        size_t payload_size = transfer->payload_size;

        int8_t des = uavcan_file_Write_Request_1_1_deserialize_(
            &req,
            static_cast<const uint8_t*>(transfer->payload),
            &payload_size);

        uavcan_file_Write_Response_1_1 resp{};
        resp._error.value = uavcan_file_Error_1_0_OK;

        if (des < 0)
        {
            resp._error.value = uavcan_file_Error_1_0_IO_ERROR;
            sendResponse(*transfer, resp);
            return;
        }

        // Convert path
        FileName path{};
        for (size_t i = 0; i < req.path.path.count && i < NAME_LENGTH - 1; i++)
            path[i] = static_cast<char>(req.path.path.elements[i]);

        // First chunk?
        if (req.offset == 0 && !active_write_)
        {
            active_write_ = stream_.initialize(path);
            if (!active_write_)
            {
                resp._error.value = uavcan_file_Error_1_0_IO_ERROR;
                sendResponse(*transfer, resp);
                return;
            }
        }

        // Write chunk
        if (active_write_)
        {
            bool ok = stream_.output(req.data.value.elements,
                                     req.data.value.count);

            if (!ok)
                resp._error.value = uavcan_file_Error_1_0_IO_ERROR;
        }

        // End of file?
        if (req.data.value.count == 0 && active_write_)
        {
            stream_.finalize();
            active_write_ = false;
        }

        sendResponse(*transfer, resp);
    }

private:
    void sendResponse(const CyphalTransfer& transfer,
                      const uavcan_file_Write_Response_1_1& resp)
    {
        constexpr size_t PAYLOAD_SIZE =
            uavcan_file_Write_Response_1_1_SERIALIZATION_BUFFER_SIZE_BYTES_;

        uint8_t payload[PAYLOAD_SIZE];

        TaskForServer<CyphalBuffer8, Adapters...>::publish(
            PAYLOAD_SIZE,
            payload,
            &resp,
            reinterpret_cast<int8_t (*)(const void*, uint8_t*, size_t*)>(
                uavcan_file_Write_Response_1_1_serialize_),
            uavcan_file_Write_1_1_FIXED_PORT_ID_,
            transfer.metadata.remote_node_id,
            transfer.metadata.transfer_id);
    }

private:
    Stream& stream_;
    bool active_write_;
};

#endif // __TASKRESPONDWRITE_HPP_