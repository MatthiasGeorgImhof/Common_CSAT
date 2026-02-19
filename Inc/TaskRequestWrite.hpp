#ifndef __TASKREQUESTWRITE_HPP_
#define __TASKREQUESTWRITE_HPP_

#include "Task.hpp"
#include "RegistrationManager.hpp"
#include "InputOutputStream.hpp"
#include "heapallocation.hpp"

#include "nunavut_assert.h"
#include "uavcan/file/Write_1_1.h"
#include "uavcan/file/Error_1_0.h"

#include <cstring>

template <InputStreamConcept InputStream, typename... Adapters>
class TaskRequestWrite : public TaskForClient<CyphalBuffer8, Adapters...>, private TaskPacing
{
public:
    enum State
    {
        IDLE = 0,
        SEND_INIT = 1,       // Initial state: Nothing is initialized
        WAIT_INIT = 2,       // waiting for initialization ok
        RESEND_INIT = 3,     // if init failed, there might be retry, also might be the start of a stream
        SEND_TRANSFER = 4,   // Ready to pull a chunk from stream
        WAIT_TRANSFER = 5,   // Sent a chunk, awaiting an OK
        RESEND_TRANSFER = 6, // chunk delivery failed and we need to send the same again
        SEND_DONE = 7,       // stream is emtpy, need to signal we are done
        WAIT_DONE = 8,       // Waiting for the final OK to complete the transmission
        RESEND_DONE = 9      // We have failed and might need to send this over and over
    };

    struct WriteState
    {
        State state;
        size_t offset;
        uint32_t timeout;
        CyphalTransferID last_transfer_id;
        uint8_t num_tries;
    };

    constexpr static unsigned int MAX_CHUNK_SIZE = 256U;
    constexpr static uint32_t TIMEOUT_FACTOR = 100U;
    constexpr static uint8_t MAX_NUM_TRIES = 5U;

public:
    TaskRequestWrite() = delete;
    TaskRequestWrite(InputStream &stream,
                     uint32_t sleep_interval,
                     uint32_t operate_interval,
                     uint32_t tick,
                     CyphalNodeID node_id,
                     CyphalTransferID transfer_id,
                     std::tuple<Adapters...> &adapters)
        : TaskForClient<CyphalBuffer8, Adapters...>(sleep_interval, tick, node_id, transfer_id, adapters),
          TaskPacing(sleep_interval, operate_interval),
          stream_(stream), total_size_(0), name_{}, write_state_(IDLE, 0, 0, 0, 0), values_{}, num_values_(0)
    {
    }

    virtual void registerTask(RegistrationManager *manager, std::shared_ptr<Task> task) override;
    virtual void unregisterTask(RegistrationManager *manager, std::shared_ptr<Task> task) override;
    virtual void handleTaskImpl() override;

    virtual void update(uint32_t now) override;

protected:
    bool request();
    bool respond();
    void reset();

    void send_init_request(uavcan_file_Write_Request_1_1 *data, size_t num_values);
    void send_transfer_request(uavcan_file_Write_Request_1_1 *data, size_t num_values);
    void send_done_request(uavcan_file_Write_Request_1_1 *data);

    bool should_restart_transfer() const;
    void restart_transfer();

    template <typename T, typename... Args>
    auto make_on_local_heap(Args &&...args)
    {
        static SafeAllocator<T, LocalHeap> alloc;
        return alloc_unique_custom<T, LocalHeap>(alloc, std::forward<Args>(args)...);
    }

protected:
    using ValueBuffer = std::array<uint8_t, uavcan_primitive_Unstructured_1_0_value_ARRAY_CAPACITY_>;
    using ValueAlloc = SafeAllocator<ValueBuffer, LocalHeap>;
    using ValuePtr = std::unique_ptr<ValueBuffer, ValueAlloc::Deletor>;

protected:
    InputStream &stream_;
    size_t total_size_;
    std::array<char, NAME_LENGTH> name_;
    WriteState write_state_;
    ValuePtr values_;
    size_t num_values_;
};

template <InputStreamConcept InputStream, typename... Adapters>
void TaskRequestWrite<InputStream, Adapters...>::reset()
{
    write_state_ = WriteState{IDLE, 0, 0, 0, 0};

    name_ = {};
    TaskPacing::sleep(*this);
}

template <InputStreamConcept InputStream, typename... Adapters>
void TaskRequestWrite<InputStream, Adapters...>::handleTaskImpl()
{
    if (values_ == nullptr)
    {
        values_ = make_on_local_heap<ValueBuffer>();
    }
    (void)respond();
    (void)request();
}

template <InputStreamConcept InputStream, typename... Adapters>
bool TaskRequestWrite<InputStream, Adapters...>::respond()
{
    log(LOG_LEVEL_DEBUG, "TaskRequestWrite: respond in state %d\r\n", write_state_.state);

    // No response available yet: handle timeout and resend transitions.
    if (TaskForClient<CyphalBuffer8, Adapters...>::buffer_.is_empty())
    {
        if (HAL_GetTick() < write_state_.timeout)

            return true;

        switch (write_state_.state)
        {
        case WAIT_INIT:
            write_state_.state = RESEND_INIT;
            break;
        case WAIT_TRANSFER:
            write_state_.state = RESEND_TRANSFER;
            break;
        case WAIT_DONE:
            write_state_.state = RESEND_DONE;
            break;
        default:
            break;
        }
        return false;
    }

    // Pop the next response.
    std::shared_ptr<CyphalTransfer> transfer = TaskForClient<CyphalBuffer8, Adapters...>::buffer_.pop();
    if (transfer->metadata.transfer_kind != CyphalTransferKindResponse)
    {
        log(LOG_LEVEL_ERROR, "TaskRequestWrite: Expected Response transfer kind\r\n");
        reset(); // 4) Reset on protocol violation
        return false;
    }

    uavcan_file_Write_Response_1_1 data;
    size_t payload_size = transfer->payload_size;

    int8_t deserialization_result =
        uavcan_file_Write_Response_1_1_deserialize_(&data,
                                                    static_cast<const uint8_t *>(transfer->payload),
                                                    &payload_size);
    if (deserialization_result < 0)
    {
        log(LOG_LEVEL_ERROR, "TaskRequestWrite: Deserialization Error\r\n");
        reset(); // 4) Reset on invalid payload
        return false;
    }

    log(LOG_LEVEL_DEBUG,
        "TaskRequestWrite: received response %d in state %d for transfer_id %d \r\n",
        data._error,
        write_state_.state,
        transfer->metadata.transfer_id);

    // 1) Validate transfer-ID
    if (transfer->metadata.transfer_id != write_state_.last_transfer_id)
    {
        log(LOG_LEVEL_ERROR,
            "TaskRequestWrite: Unexpected transfer-ID: expected %d, got %d\r\n",
            write_state_.last_transfer_id,
            transfer->metadata.transfer_id);
        reset(); // 4) Reset on unexpected transfer-ID
        return false;
    }

    // 2) Reject responses in invalid states
    if (write_state_.state != WAIT_INIT &&
        write_state_.state != WAIT_TRANSFER &&
        write_state_.state != WAIT_DONE)
    {
        log(LOG_LEVEL_ERROR,
            "TaskRequestWrite: Response received in invalid state %d\r\n",
            write_state_.state);
        reset(); // 4) Reset on unexpected state
        return false;
    }

    // Normal state machine progression.
    if (data._error.value == uavcan_file_Error_1_0_OK)
    {
        switch (write_state_.state)
        {
        case WAIT_INIT:
            write_state_.state = SEND_TRANSFER;
            break;

        case WAIT_TRANSFER:
            write_state_.state = SEND_TRANSFER;
            break;

        case WAIT_DONE:
            stream_.finalize();
            reset();
            break;

        default:
            // Should be unreachable due to the state check above.
            reset();
            return false;
        }
    }
    else
    {
        switch (write_state_.state)
        {
        case WAIT_INIT:
            write_state_.state = RESEND_INIT;
            break;

        case WAIT_TRANSFER:
            write_state_.state = RESEND_TRANSFER;
            break;

        case WAIT_DONE:
            write_state_.state = RESEND_DONE;
            break;

        default:
            // Should be unreachable due to the state check above.
            reset();
            return false;
        }
    }

    return true;
}

template <InputStreamConcept InputStream, typename... Adapters>
void TaskRequestWrite<InputStream, Adapters...>::send_init_request(uavcan_file_Write_Request_1_1 *data, size_t num_values)
{
    data->data.value.count = num_values;
    data->path.path.count = NAME_LENGTH;
    std::memcpy(data->path.path.elements, name_.data(), NAME_LENGTH);
    std::memcpy(data->data.value.elements, values_->data(), num_values);
    write_state_.state = WAIT_INIT;
}

template <InputStreamConcept InputStream, typename... Adapters>
void TaskRequestWrite<InputStream, Adapters...>::send_transfer_request(uavcan_file_Write_Request_1_1 *data, size_t num_values)
{
    data->data.value.count = num_values;
    data->path.path.count = NAME_LENGTH;
    std::memcpy(data->path.path.elements, name_.data(), NAME_LENGTH);
    std::memcpy(data->data.value.elements, values_->data(), num_values);
    write_state_.state = WAIT_TRANSFER;
}

template <InputStreamConcept InputStream, typename... Adapters>
void TaskRequestWrite<InputStream, Adapters...>::send_done_request(uavcan_file_Write_Request_1_1 *data)
{
    data->data.value.count = 0;
    data->path.path.count = NAME_LENGTH;
    std::memcpy(data->path.path.elements, name_.data(), NAME_LENGTH);
    write_state_.state = WAIT_DONE;
}

template <InputStreamConcept InputStream, typename... Adapters>
bool TaskRequestWrite<InputStream, Adapters...>::should_restart_transfer() const
{
    return write_state_.num_tries > MAX_NUM_TRIES;
}

template <InputStreamConcept InputStream, typename... Adapters>
void TaskRequestWrite<InputStream, Adapters...>::restart_transfer()
{
    log(LOG_LEVEL_ERROR, "TaskRequestWrite: retry budget exceeded, restarting transfer\r\n");
    write_state_.state = SEND_INIT;
    write_state_.offset = 0;
    write_state_.num_tries = 0;
}

template <InputStreamConcept InputStream, typename... Adapters>
bool TaskRequestWrite<InputStream, Adapters...>::request()
{
    log(LOG_LEVEL_DEBUG, "TaskRequestWrite: request in state %d\r\n", write_state_.state);

    if (write_state_.state == WAIT_INIT || write_state_.state == WAIT_TRANSFER || write_state_.state == WAIT_DONE)
        return false;

    if (!TaskForClient<CyphalBuffer8, Adapters...>::buffer_.is_empty()) // Should have been emtpied by respond
        return false;

    if (!stream_.is_empty() && write_state_.state == IDLE)
    {
        write_state_.state = SEND_INIT;
        log(LOG_LEVEL_DEBUG, "TaskRequestWrite: data available\r\n");
        TaskPacing::operate(*this);
    }

    auto data = make_on_local_heap<uavcan_file_Write_Request_1_1>();

    log(LOG_LEVEL_DEBUG, "TaskRequestWrite: request in state %d\r\n", write_state_.state);
    switch (write_state_.state)
    {
    case SEND_INIT:
    {
        stream_.initialize(values_->data(), num_values_);
        name_ = stream_.name();
        total_size_ = stream_.size();

        data->offset = write_state_.offset;
        send_init_request(data.get(), num_values_);
        write_state_.offset += num_values_;
        write_state_.num_tries = 0;
        break;
    }
    case RESEND_INIT:
        data->offset = write_state_.offset - num_values_;
        send_init_request(data.get(), num_values_);
        write_state_.num_tries++;

        if (should_restart_transfer())
        {
            restart_transfer();
            return false;
        }
        break;

    case SEND_TRANSFER:
    {
        num_values_ = std::min(MAX_CHUNK_SIZE, uavcan_primitive_Unstructured_1_0_value_ARRAY_CAPACITY_);
        stream_.getChunk(values_->data(), num_values_);
        if (num_values_ == 0)
        {
            write_state_.state = SEND_DONE;
            goto send_done_label;
        }

        data->offset = write_state_.offset;
        send_transfer_request(data.get(), num_values_);
        write_state_.offset += num_values_;
        write_state_.num_tries = 0;
        break;
    }
    case RESEND_TRANSFER:
        data->offset = write_state_.offset - num_values_;
        send_transfer_request(data.get(), num_values_);
        write_state_.num_tries++;
        
        if (should_restart_transfer())
        {
            restart_transfer();
            return false;
        }
        break;

    send_done_label:
    case SEND_DONE:
        data->offset = write_state_.offset;
        send_done_request(data.get());
        write_state_.num_tries = 0;
        break;

    case RESEND_DONE:
        data->offset = write_state_.offset;
        send_done_request(data.get());
        write_state_.num_tries++;

        if (should_restart_transfer())
        {
            restart_transfer();
            return false;
        }
        break;

    default:
        return false; // catch it all
    }

    write_state_.last_transfer_id = this->transfer_id_;
    constexpr size_t PAYLOAD_SIZE = uavcan_file_Write_Request_1_1_SERIALIZATION_BUFFER_SIZE_BYTES_;
    uint8_t payload[PAYLOAD_SIZE];
    TaskForClient<CyphalBuffer8, Adapters...>::publish(
        PAYLOAD_SIZE,
        payload,
        data.get(),
        reinterpret_cast<int8_t (*)(const void *const, uint8_t *const, size_t *const)>(
            uavcan_file_Write_Request_1_1_serialize_),
        uavcan_file_Write_1_1_FIXED_PORT_ID_,
        TaskForClient<CyphalBuffer8, Adapters...>::node_id_);
    log(LOG_LEVEL_DEBUG, "TaskRequestWrite: sent request with %d bytes at offset %d and transfer_id %d\r\n", data->data.value.count, write_state_.offset - data->data.value.count, this->transfer_id_);
    write_state_.timeout = HAL_GetTick() + TIMEOUT_FACTOR * Task::interval_;
    ++this->transfer_id_;
    return true;
}

template <InputStreamConcept InputStream, typename... Adapters>
void TaskRequestWrite<InputStream, Adapters...>::registerTask(RegistrationManager *manager, std::shared_ptr<Task> task)
{
    manager->client(uavcan_file_Write_1_1_FIXED_PORT_ID_, task);
}

template <InputStreamConcept InputStream, typename... Adapters>
void TaskRequestWrite<InputStream, Adapters...>::unregisterTask(RegistrationManager *manager, std::shared_ptr<Task> task)
{
    manager->unclient(uavcan_file_Write_1_1_FIXED_PORT_ID_, task);
}

template <InputStreamConcept InputStream, typename... Adapters>
void TaskRequestWrite<InputStream, Adapters...>::update(uint32_t now)
{
    Task::update(now);
}

#endif // __TASKREQUESTWRITE_HPP_
