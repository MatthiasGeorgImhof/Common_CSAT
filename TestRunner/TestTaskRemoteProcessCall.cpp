#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <tuple>

#include "RegistrationManager.hpp"
#include "ServiceManager.hpp"
#include "TaskRemoteProcessCall.hpp"
#include "cyphal.hpp"
#include "loopard_adapter.hpp"

#include "uavcan/node/ExecuteCommand_1_3.h"

#ifdef __x86_64__
#include "mock_hal.h"
#endif

namespace
{
struct DummyDevice
{
    int  calls  = 0;
    int  step   = 1;
    bool result = true;

    bool run()
    {
        calls += step;
        return result;
    }
};

int g_free_calls = 0;
bool free_rpc_fn()
{
    ++g_free_calls;
    return true;
}

bool failing_rpc_fn()
{
    return false;
}
} // namespace

void* loopardMemoryAllocate(std::size_t amount) { return std::malloc(amount); }
void loopardMemoryFree(void* pointer) { std::free(pointer); }

std::shared_ptr<CyphalTransfer> createExecuteCommandRequest(std::uint16_t command,
                                                            std::uint8_t transfer_id = 0)
{
    auto transfer = std::make_shared<CyphalTransfer>();
    transfer->metadata.transfer_kind  = CyphalTransferKindRequest;
    transfer->metadata.port_id        = uavcan_node_ExecuteCommand_1_3_FIXED_PORT_ID_;
    transfer->metadata.remote_node_id = 123;
    transfer->metadata.transfer_id    = transfer_id;

    uavcan_node_ExecuteCommand_Request_1_3 request{};
    request.command = command;

    transfer->payload_size = uavcan_node_ExecuteCommand_Request_1_3_SERIALIZATION_BUFFER_SIZE_BYTES_;
    transfer->payload      = loopardMemoryAllocate(transfer->payload_size);
    REQUIRE(transfer->payload != nullptr);

    const std::int8_t res = uavcan_node_ExecuteCommand_Request_1_3_serialize_(
        &request,
        static_cast<std::uint8_t*>(transfer->payload),
        &transfer->payload_size);
    REQUIRE(res >= 0);

    return transfer;
}

uavcan_node_ExecuteCommand_Response_1_3 decodeExecuteCommandResponse(CyphalTransfer& response)
{
    CHECK(response.metadata.port_id == uavcan_node_ExecuteCommand_1_3_FIXED_PORT_ID_);
    CHECK(response.metadata.transfer_kind == CyphalTransferKindResponse);
    CHECK(response.payload_size <= uavcan_node_ExecuteCommand_Response_1_3_SERIALIZATION_BUFFER_SIZE_BYTES_);

    uavcan_node_ExecuteCommand_Response_1_3 received{};
    std::size_t deserialized_size = response.payload_size;
    const std::int8_t res = uavcan_node_ExecuteCommand_Response_1_3_deserialize_(
        &received,
        static_cast<const std::uint8_t*>(response.payload),
        &deserialized_size);
    REQUIRE(res >= 0);

    loopardMemoryFree(response.payload);
    return received;
}

template <typename... Adapters>
class MockTaskRemoteProcessCall : public TaskRemoteProcessCall<Adapters...>
{
public:
    MockTaskRemoteProcessCall(std::span<const RpcEntry> entries,
                              std::uint32_t interval,
                              std::uint32_t tick,
                              std::tuple<Adapters...>& adapters)
        : TaskRemoteProcessCall<Adapters...>(entries, interval, tick, adapters) {}

    using TaskRemoteProcessCall<Adapters...>::handleMessage;
    std::size_t getBufferSize() const { return TaskRemoteProcessCall<Adapters...>::buffer_.size(); }
};

TEST_CASE("TaskRemoteProcessCall: dispatches matching ExecuteCommand and returns status")
{
    LoopardAdapter loopard1;
    loopard1.memory_allocate = loopardMemoryAllocate;
    loopard1.memory_free     = loopardMemoryFree;

    Cyphal<LoopardAdapter> loopard_cyphal1(&loopard1);
    loopard_cyphal1.setNodeID(11);
    std::tuple<Cyphal<LoopardAdapter>> adapters(loopard_cyphal1);

    DummyDevice dev_a{0, 2, true};
    DummyDevice dev_b{10, 5, false};
    g_free_calls = 0;

    const std::array<RpcEntry, 5> table = {
        make_rpc_entry<&DummyDevice::run>(10, dev_a),
        make_rpc_entry<&DummyDevice::run>(20, dev_b),
        make_rpc_entry<free_rpc_fn>(30),
        make_rpc_entry<failing_rpc_fn>(40),
        RpcEntry{50, nullptr, nullptr}
    };

    MockTaskRemoteProcessCall task(table, 250, 0, adapters);

    auto send_cmd = [&](std::uint16_t cmd, std::uint8_t tid) {
        auto req = createExecuteCommandRequest(cmd, tid);
        task.handleMessage(req);
        REQUIRE(task.getBufferSize() == 1);
        task.handleTaskImpl();
        loopardMemoryFree(req->payload);

        REQUIRE(loopard1.buffer.size() == 1);
        auto transfer = loopard1.buffer.pop();
        return decodeExecuteCommandResponse(transfer);
    };

    // 1. Command 10 -> dev_a returns true -> SUCCESS, dev_b untouched
    auto r1 = send_cmd(10, 1);
    CHECK(r1.status == uavcan_node_ExecuteCommand_Response_1_3_STATUS_SUCCESS);
    CHECK(dev_a.calls  == 2);
    CHECK(dev_b.calls  == 10);
    CHECK(g_free_calls == 0);

    // 2. Command 20 -> dev_b returns false -> FAILURE
    auto r2 = send_cmd(20, 2);
    CHECK(r2.status == uavcan_node_ExecuteCommand_Response_1_3_STATUS_FAILURE);
    CHECK(dev_b.calls == 15);

    // 3. Command 30 -> free function returns true -> SUCCESS
    auto r3 = send_cmd(30, 3);
    CHECK(r3.status == uavcan_node_ExecuteCommand_Response_1_3_STATUS_SUCCESS);
    CHECK(g_free_calls == 1);

    // 4. Command 40 -> free function returns false -> FAILURE
    auto r4 = send_cmd(40, 4);
    CHECK(r4.status == uavcan_node_ExecuteCommand_Response_1_3_STATUS_FAILURE);

    // 5. Command 50 (null callback) and 999 (unknown) -> BAD_COMMAND
    auto r5 = send_cmd(50, 5);
    CHECK(r5.status == uavcan_node_ExecuteCommand_Response_1_3_STATUS_BAD_COMMAND);

    auto r6 = send_cmd(999, 6);
    CHECK(r6.status == uavcan_node_ExecuteCommand_Response_1_3_STATUS_BAD_COMMAND);
}

TEST_CASE("TaskRemoteProcessCall: registers and unregisters ExecuteCommand server")
{
    RegistrationManager registration_manager;
    LoopardAdapter loopard1;
    loopard1.memory_allocate = loopardMemoryAllocate;
    loopard1.memory_free     = loopardMemoryFree;

    Cyphal<LoopardAdapter> loopard_cyphal1(&loopard1);
    loopard_cyphal1.setNodeID(11);
    std::tuple<Cyphal<LoopardAdapter>> adapters(loopard_cyphal1);

    const std::array<RpcEntry, 1> table = {
        make_rpc_entry<free_rpc_fn>(0)
    };

    auto task = std::make_shared<TaskRemoteProcessCall<Cyphal<LoopardAdapter>>>(
        table, 100, 0, adapters);

    CHECK(registration_manager.getServers().size() == 0);

    task->registerTask(&registration_manager, task);
    CHECK(registration_manager.getServers().size() == 1);
    CHECK(registration_manager.getServers().containsIf([&](const CyphalPortID& port_id) {
        return port_id == uavcan_node_ExecuteCommand_1_3_FIXED_PORT_ID_;
    }));

    task->unregisterTask(&registration_manager, task);
    CHECK(registration_manager.getServers().size() == 0);
}