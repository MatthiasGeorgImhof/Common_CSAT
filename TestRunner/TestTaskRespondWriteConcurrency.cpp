#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <memory>
#include <tuple>
#include <cstring>

#include "TaskRespondWrite.hpp"
#include "Task.hpp"
#include "cyphal.hpp"
#include "loopard_adapter.hpp"
#include "RegistrationManager.hpp"
#include "InputOutputStream.hpp"
#include "heapallocation.hpp"

#include "uavcan/file/Write_1_1.h"

using LocalHeap = HeapAllocation<65536>;

// -----------------------------------------------------------------------------
// Helper: create a Write.Request with given parameters
// -----------------------------------------------------------------------------
static std::shared_ptr<CyphalTransfer> makeWriteRequest(
    CyphalNodeID writer,
    CyphalTransferID tid,
    const char* path,
    uint64_t offset,
    const uint8_t* data,
    size_t count)
{
    uavcan_file_Write_Request_1_1 req{};
    req.offset = offset;

    size_t len = std::min<size_t>(strlen(path), NAME_LENGTH - 1);
    req.path.path.count = len;
    for (size_t i = 0; i < len; i++)
        req.path.path.elements[i] = static_cast<uint8_t>(path[i]);   // fix sign-conversion

    req.data.value.count = count;
    for (size_t i = 0; i < count; i++)
        req.data.value.elements[i] = data ? data[i] : 0;

    uint8_t payload[uavcan_file_Write_Request_1_1_SERIALIZATION_BUFFER_SIZE_BYTES_];
    size_t payload_size = sizeof(payload);

    int8_t res = uavcan_file_Write_Request_1_1_serialize_(
        &req, payload, &payload_size);
    REQUIRE(res >= 0);

    auto transfer = std::make_shared<CyphalTransfer>();
    transfer->payload = malloc(payload_size);
    memcpy(transfer->payload, payload, payload_size);
    transfer->payload_size = payload_size;

    transfer->metadata.transfer_kind = CyphalTransferKindRequest;
    transfer->metadata.remote_node_id = writer;
    transfer->metadata.transfer_id = tid;

    return transfer;
}

// -----------------------------------------------------------------------------
// Helper: unpack Write.Response
// -----------------------------------------------------------------------------
static uavcan_file_Write_Response_1_1 unpackResponse(std::shared_ptr<CyphalTransfer> transfer)
{
    uavcan_file_Write_Response_1_1 resp{};
    size_t sz = transfer->payload_size;
    int8_t res = uavcan_file_Write_Response_1_1_deserialize_(
        &resp,
        static_cast<const uint8_t*>(transfer->payload),
        &sz);
    REQUIRE(res >= 0);
    return resp;
}

// -----------------------------------------------------------------------------
// Mock TaskRespondWrite exposing protected buffer_
// -----------------------------------------------------------------------------
template <typename Heap, OutputStreamConcept Stream, typename... Adapters>
class MockTaskRespondWrite : public TaskRespondWrite<Heap, Stream, Adapters...>
{
public:
    MockTaskRespondWrite(Stream &stream, uint32_t interval, uint32_t tick, std::tuple<Adapters...> &adapters)
        : TaskRespondWrite<Heap, Stream, Adapters...>(stream, interval, tick, adapters)
    {}

    using TaskRespondWrite<Heap, Stream, Adapters...>::handleTaskImpl;
    using TaskRespondWrite<Heap, Stream, Adapters...>::buffer_;
};

// -----------------------------------------------------------------------------
// TESTS
// -----------------------------------------------------------------------------

TEST_CASE("TaskRespondWrite: Rejects second writer during active session")
{
    LocalHeap::initialize();

    LoopardAdapter loopard;
    loopard.memory_allocate = malloc;
    loopard.memory_free = free;
    Cyphal<LoopardAdapter> cyphal(&loopard);

    std::tuple<Cyphal<LoopardAdapter>> adapters(cyphal);
    TrivialOutputStream out;

    MockTaskRespondWrite<LocalHeap, TrivialOutputStream, Cyphal<LoopardAdapter>>
        task(out, 1000, 0, adapters);

    // Writer A starts a session
    uint8_t dataA[4] = {1,2,3,4};
    auto reqA0 = makeWriteRequest(10, 1, "config", 0, dataA, 4);
    task.buffer_.push(reqA0);
    task.handleTaskImpl();

    auto respA0 = unpackResponse(std::make_shared<CyphalTransfer>(loopard.buffer.pop()));
    CHECK(respA0._error.value == uavcan_file_Error_1_0_OK);
    loopard.buffer.clear();

    // Writer B tries to start a session while A is active
    uint8_t dataB[4] = {9,9,9,9};
    auto reqB0 = makeWriteRequest(22, 1, "config", 0, dataB, 4);
    task.buffer_.push(reqB0);
    task.handleTaskImpl();

    auto respB0 = unpackResponse(std::make_shared<CyphalTransfer>(loopard.buffer.pop()));
    CHECK(respB0._error.value == uavcan_file_Error_1_0_IO_ERROR);
    loopard.buffer.clear();

    // Writer A sends EOF
    auto reqAEOF = makeWriteRequest(10, 2, "config", 4, nullptr, 0);
    task.buffer_.push(reqAEOF);
    task.handleTaskImpl();

    auto respAEOF = unpackResponse(std::make_shared<CyphalTransfer>(loopard.buffer.pop()));
    CHECK(respAEOF._error.value == uavcan_file_Error_1_0_OK);
    loopard.buffer.clear();

    // Now writer B may start a new session
    auto reqB1 = makeWriteRequest(22, 3, "config", 0, dataB, 4);
    task.buffer_.push(reqB1);
    task.handleTaskImpl();

    auto respB1 = unpackResponse(std::make_shared<CyphalTransfer>(loopard.buffer.pop()));
    CHECK(respB1._error.value == uavcan_file_Error_1_0_OK);
}

TEST_CASE("TaskRespondWrite: Rejects continuation with wrong path")
{
    LocalHeap::initialize();

    LoopardAdapter loopard;
    loopard.memory_allocate = malloc;
    loopard.memory_free = free;
    Cyphal<LoopardAdapter> cyphal(&loopard);

    std::tuple<Cyphal<LoopardAdapter>> adapters(cyphal);
    TrivialOutputStream out;

    MockTaskRespondWrite<LocalHeap, TrivialOutputStream, Cyphal<LoopardAdapter>>
        task(out, 1000, 0, adapters);

    uint8_t data[4] = {1,2,3,4};

    // Start session on "config"
    auto req0 = makeWriteRequest(10, 1, "config", 0, data, 4);
    task.buffer_.push(req0);
    task.handleTaskImpl();
    loopard.buffer.clear();

    // Wrong path continuation
    auto reqWrong = makeWriteRequest(10, 2, "state", 4, data, 4);
    task.buffer_.push(reqWrong);
    task.handleTaskImpl();

    auto respWrong = unpackResponse(std::make_shared<CyphalTransfer>(loopard.buffer.pop()));
    CHECK(respWrong._error.value == uavcan_file_Error_1_0_IO_ERROR);
}

TEST_CASE("TaskRespondWrite: Rejects continuation from wrong writer")
{
    LocalHeap::initialize();

    LoopardAdapter loopard;
    loopard.memory_allocate = malloc;
    loopard.memory_free = free;
    Cyphal<LoopardAdapter> cyphal(&loopard);

    std::tuple<Cyphal<LoopardAdapter>> adapters(cyphal);
    TrivialOutputStream out;

    MockTaskRespondWrite<LocalHeap, TrivialOutputStream, Cyphal<LoopardAdapter>>
        task(out, 1000, 0, adapters);

    uint8_t data[4] = {1,2,3,4};

    // Writer A starts session
    auto reqA0 = makeWriteRequest(10, 1, "config", 0, data, 4);
    task.buffer_.push(reqA0);
    task.handleTaskImpl();
    loopard.buffer.clear();

    // Writer B tries to continue
    auto reqB1 = makeWriteRequest(22, 1, "config", 4, data, 4);
    task.buffer_.push(reqB1);
    task.handleTaskImpl();

    auto respB1 = unpackResponse(std::make_shared<CyphalTransfer>(loopard.buffer.pop()));
    CHECK(respB1._error.value == uavcan_file_Error_1_0_IO_ERROR);
}
