#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "RecordRegistry.hpp"
#include "TaskRespondRegisterList.hpp"
#include "Task.hpp"
#include "cyphal.hpp"
#include "loopard_adapter.hpp"
#include "RegistrationManager.hpp"
#include "ServiceManager.hpp"

#include "uavcan/_register/List_1_0.h"

#ifdef __x86_64__
#include "mock_hal.h"
#endif

void* loopardMemoryAllocate(size_t amount) { return std::malloc(amount); }
void loopardMemoryFree(void* pointer) { std::free(pointer); }

// ------------------------------------------------------------
// Test MRAM layout & registry content
// ------------------------------------------------------------
struct reg_config_t  { std::uint32_t a; };
struct reg_command_t { std::uint32_t b; };
struct reg_state_t   { std::uint32_t c; };
struct reg_log_t     { std::uint32_t d; };

struct TestRegisterLayout
{
    StorageRecord<reg_config_t>  config;
    StorageRecord<reg_command_t> command;
    StorageRecord<reg_state_t>   state;
    StorageRecord<reg_log_t>     log;
};

template <>
struct LayoutMembers<TestRegisterLayout>
{
    static constexpr auto members = std::make_tuple(
        &TestRegisterLayout::config,
        &TestRegisterLayout::command,
        &TestRegisterLayout::state,
        &TestRegisterLayout::log
    );
};

template <>
struct RecordTraits<reg_config_t, TestRegisterLayout>
{
    static constexpr std::uint32_t offset  = offsetof(TestRegisterLayout, config);
    static constexpr std::uint32_t version = 1;
    static constexpr std::size_t   index   = 0;
    static constexpr const char*   name    = "config";
};

template <>
struct RecordTraits<reg_command_t, TestRegisterLayout>
{
    static constexpr std::uint32_t offset  = offsetof(TestRegisterLayout, command);
    static constexpr std::uint32_t version = 1;
    static constexpr std::size_t   index   = 1;
    static constexpr const char*   name    = "command";
};

template <>
struct RecordTraits<reg_state_t, TestRegisterLayout>
{
    static constexpr std::uint32_t offset  = offsetof(TestRegisterLayout, state);
    static constexpr std::uint32_t version = 1;
    static constexpr std::size_t   index   = 2;
    static constexpr const char*   name    = "state";
};

template <>
struct RecordTraits<reg_log_t, TestRegisterLayout>
{
    static constexpr std::uint32_t offset  = offsetof(TestRegisterLayout, log);
    static constexpr std::uint32_t version = 1;
    static constexpr std::size_t   index   = 3;
    static constexpr const char*   name    = "log";
};

// ------------------------------------------------------------
// Helpers
// ------------------------------------------------------------
std::shared_ptr<CyphalTransfer> createListRequest(std::uint16_t index, std::uint8_t transfer_id = 0)
{
    auto transfer = std::make_shared<CyphalTransfer>();
    transfer->metadata.transfer_kind  = CyphalTransferKindRequest;
    transfer->metadata.port_id        = uavcan_register_List_1_0_FIXED_PORT_ID_;
    transfer->metadata.remote_node_id = 123;
    transfer->metadata.transfer_id    = transfer_id;

    uavcan_register_List_Request_1_0 request{};
    request.index = index;

    transfer->payload_size = uavcan_register_List_Request_1_0_SERIALIZATION_BUFFER_SIZE_BYTES_;
    transfer->payload = loopardMemoryAllocate(transfer->payload_size);
    REQUIRE(transfer->payload != nullptr);

    const int8_t res = uavcan_register_List_Request_1_0_serialize_(
        &request,
        static_cast<std::uint8_t*>(transfer->payload),
        &transfer->payload_size);
    REQUIRE(res >= 0);

    return transfer;
}

std::string decodeListResponse(CyphalTransfer& response)
{
    CHECK(response.metadata.port_id == uavcan_register_List_1_0_FIXED_PORT_ID_);
    CHECK(response.metadata.transfer_kind == CyphalTransferKindResponse);
    CHECK(response.payload_size <= uavcan_register_List_Response_1_0_SERIALIZATION_BUFFER_SIZE_BYTES_);

    uavcan_register_List_Response_1_0 received{};
    size_t deserialized_size = response.payload_size;
    const int8_t res = uavcan_register_List_Response_1_0_deserialize_(
        &received,
        static_cast<const std::uint8_t*>(response.payload),
        &deserialized_size);
    REQUIRE(res >= 0);

    std::string out(reinterpret_cast<const char*>(received.name.name.elements),
                    received.name.name.count);
    loopardMemoryFree(response.payload);
    return out;
}

template <typename Registry, typename... Adapters>
class MockTaskRespondRegisterList : public TaskRespondRegisterList<Registry, Adapters...>
{
public:
    MockTaskRespondRegisterList(Registry& registry,
                                uint32_t interval,
                                uint32_t tick,
                                std::tuple<Adapters...>& adapters)
        : TaskRespondRegisterList<Registry, Adapters...>(registry, interval, tick, adapters) {}

    using TaskRespondRegisterList<Registry, Adapters...>::handleMessage;
    size_t getBufferSize() const { return TaskRespondRegisterList<Registry, Adapters...>::buffer_.size(); }
    using TaskRespondRegisterList<Registry, Adapters...>::buffer_;
};

// ------------------------------------------------------------
// Tests
// ------------------------------------------------------------
TEST_CASE("TaskRespondRegisterList: loops over indices until empty name and matches expected")
{
    LoopardAdapter loopard1, loopard2;
    loopard1.memory_allocate = loopardMemoryAllocate;
    loopard1.memory_free     = loopardMemoryFree;
    loopard2.memory_allocate = loopardMemoryAllocate;
    loopard2.memory_free     = loopardMemoryFree;

    Cyphal<LoopardAdapter> loopard_cyphal1(&loopard1), loopard_cyphal2(&loopard2);
    loopard_cyphal1.setNodeID(11);
    loopard_cyphal2.setNodeID(12);
    std::tuple<Cyphal<LoopardAdapter>, Cyphal<LoopardAdapter>> adapters(loopard_cyphal1, loopard_cyphal2);

    RecordRegistry<TestRegisterLayout> registry;
    MockTaskRespondRegisterList task(registry, 1000, 0, adapters);

    const std::vector<std::string> expected_names = {"config", "command", "state", "log"};
    std::vector<std::string> names_adapter1;
    std::vector<std::string> names_adapter2;

    for (std::uint16_t idx = 0;; ++idx)
    {
        auto request = createListRequest(idx, static_cast<std::uint8_t>(idx));
        task.handleMessage(request);
        REQUIRE(task.getBufferSize() == 1);

        task.handleTaskImpl();
        loopardMemoryFree(request->payload);

        REQUIRE(loopard1.buffer.size() == 1);
        REQUIRE(loopard2.buffer.size() == 1);

        CyphalTransfer resp1 = loopard1.buffer.pop();
        CyphalTransfer resp2 = loopard2.buffer.pop();

        std::string name1 = decodeListResponse(resp1);
        std::string name2 = decodeListResponse(resp2);
        CHECK(name1 == name2);

        if (name1.empty())
            break;

        names_adapter1.push_back(std::move(name1));
        names_adapter2.push_back(std::move(name2));
    }

    CHECK(names_adapter1 == expected_names);
    CHECK(names_adapter2 == expected_names);

    // Out-of-bounds index well past the end also returns an empty name
    auto oob_req = createListRequest(100, 99);
    task.handleMessage(oob_req);
    task.handleTaskImpl();
    loopardMemoryFree(oob_req->payload);

    REQUIRE(loopard1.buffer.size() == 1);
    REQUIRE(loopard2.buffer.size() == 1);
    CyphalTransfer oob_resp1 = loopard1.buffer.pop();
    CyphalTransfer oob_resp2 = loopard2.buffer.pop();
    CHECK(decodeListResponse(oob_resp1).empty());
    CHECK(decodeListResponse(oob_resp2).empty());
}

TEST_CASE("TaskRespondRegisterList: registers and unregisters correctly")
{
    RegistrationManager registration_manager;

    LoopardAdapter loopard1, loopard2;
    loopard1.memory_allocate = loopardMemoryAllocate;
    loopard1.memory_free     = loopardMemoryFree;
    loopard2.memory_allocate = loopardMemoryAllocate;
    loopard2.memory_free     = loopardMemoryFree;

    Cyphal<LoopardAdapter> loopard_cyphal1(&loopard1), loopard_cyphal2(&loopard2);
    loopard_cyphal1.setNodeID(11);
    loopard_cyphal2.setNodeID(12);
    std::tuple<Cyphal<LoopardAdapter>, Cyphal<LoopardAdapter>> adapters(loopard_cyphal1, loopard_cyphal2);

    RecordRegistry<TestRegisterLayout> registry;
    auto task = std::make_shared<TaskRespondRegisterList<
        RecordRegistry<TestRegisterLayout>,
        Cyphal<LoopardAdapter>,
        Cyphal<LoopardAdapter>>>(registry, 1000, 0, adapters);

    CHECK(registration_manager.getServers().size() == 0);

    task->registerTask(&registration_manager, task);
    CHECK(registration_manager.getServers().size() == 1);
    CHECK(registration_manager.getServers().containsIf([&](const CyphalPortID& port_id) {
        return port_id == uavcan_register_List_1_0_FIXED_PORT_ID_;
    }));

    task->unregisterTask(&registration_manager, task);
    CHECK(registration_manager.getServers().size() == 0);
}