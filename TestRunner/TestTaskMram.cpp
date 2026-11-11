#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "Mram.hpp"
#include "TaskMram.hpp"
#include "RamMramMock.hpp"

struct mram_config_t  { uint32_t a; };
struct mram_command_t { uint32_t b; };
struct mram_state_t   { uint32_t c; };
struct mram_log_t     { uint8_t  d[8]; };

struct MramLayout
{
    MramRecord<mram_config_t>   config;
    MramRecord<mram_command_t>  command;
    MramRecord<mram_state_t>    state;
    MramRecord<mram_log_t>      log;
};

template <>
struct MramTraits<mram_config_t, MramLayout>
{
    static constexpr uint32_t offset  = offsetof(MramLayout, config);
    static constexpr uint32_t version = 1;
    static constexpr std::size_t index = 0;
    static constexpr const char* name = "config";
};

template <>
struct MramTraits<mram_command_t, MramLayout>
{
    static constexpr uint32_t offset  = offsetof(MramLayout, command);
    static constexpr uint32_t version = 1;
    static constexpr std::size_t index = 1;
    static constexpr const char* name = "command";
};

template <>
struct MramTraits<mram_state_t, MramLayout>
{
    static constexpr uint32_t offset  = offsetof(MramLayout, state);
    static constexpr uint32_t version = 1;
    static constexpr std::size_t index = 2;
    static constexpr const char* name = "state";
};

template <>
struct MramTraits<mram_log_t, MramLayout>
{
    static constexpr uint32_t offset  = offsetof(MramLayout, log);
    static constexpr uint32_t version = 1;
    static constexpr std::size_t index = 3;
    static constexpr const char* name = "log";
};

template <typename Layout>
std::optional<uint32_t> mram_lookup_offset(std::string_view name)
{
    if (name == MramTraits<mram_config_t, Layout>::name)
        return MramTraits<mram_config_t, Layout>::offset;
    if (name == MramTraits<mram_command_t, Layout>::name)
        return MramTraits<mram_command_t, Layout>::offset;
    if (name == MramTraits<mram_state_t, Layout>::name)
        return MramTraits<mram_state_t, Layout>::offset;
    if (name == MramTraits<mram_log_t, Layout>::name)
        return MramTraits<mram_log_t, Layout>::offset;

    return std::nullopt;
}

struct MockTransfer : public CyphalTransfer
{
    MockTransfer(uint16_t port_id,
                 uint8_t* payload,
                 size_t payload_size)
    {
        metadata.port_id = port_id;
        metadata.transfer_kind = CyphalTransferKindRequest;
        metadata.remote_node_id = 42;
        metadata.transfer_id = 7;

        this->payload = payload;
        this->payload_size = payload_size;
    }
};

template <typename Transport, typename Layout, typename... Adapters>
class MockTaskMram : public TaskMram<Transport, Layout, Adapters...>
{
    using Base    = TaskMram<Transport, Layout, Adapters...>;
    using Backend = typename Base::Backend;

public:
    MockTaskMram(Backend& backend,
                 uint32_t interval,
                 uint32_t tick,
                 std::tuple<Adapters...>& adapters)
        : Base(backend, interval, tick, adapters)
    {}

    void handleWrite(const CyphalTransfer& transfer)
    {
        // Access protected base implementation for testing
        Base::handleWrite(transfer);
    }

    void handleRead(const CyphalTransfer& transfer)
    {
        // Access protected base implementation for testing
        Base::handleRead(transfer);
    }
};

TEST_CASE("MramTraits resolves offsets, names, and indices")
{
    using L = MramLayout;

    CHECK(MramTraits<mram_config_t,  L>::offset == offsetof(L, config));
    CHECK(MramTraits<mram_command_t, L>::offset == offsetof(L, command));
    CHECK(MramTraits<mram_state_t,   L>::offset == offsetof(L, state));
    CHECK(MramTraits<mram_log_t,     L>::offset == offsetof(L, log));

    CHECK(std::string_view(MramTraits<mram_config_t,  L>::name) == "config");
    CHECK(std::string_view(MramTraits<mram_command_t, L>::name) == "command");
    CHECK(std::string_view(MramTraits<mram_state_t,   L>::name) == "state");
    CHECK(std::string_view(MramTraits<mram_log_t,     L>::name) == "log");

    CHECK(MramTraits<mram_config_t,  L>::index == 0);
    CHECK(MramTraits<mram_command_t, L>::index == 1);
    CHECK(MramTraits<mram_state_t,   L>::index == 2);
    CHECK(MramTraits<mram_log_t,     L>::index == 3);
}

TEST_CASE("mram_lookup_offset works")
{
    using L = MramLayout;

    CHECK(mram_lookup_offset<L>("config").value()  == offsetof(L, config));
    CHECK(mram_lookup_offset<L>("command").value() == offsetof(L, command));
    CHECK(mram_lookup_offset<L>("state").value()   == offsetof(L, state));
    CHECK(mram_lookup_offset<L>("log").value()     == offsetof(L, log));

    CHECK(!mram_lookup_offset<L>("does_not_exist").has_value());
}

TEST_CASE("RamMramMock read/write works")
{
    RamMramMock<1024> mock;

    uint8_t w[4] = {1,2,3,4};
    CHECK(mock.writeBytes(10, w, 4));

    uint8_t r[4] = {};
    CHECK(mock.readBytes(10, r, 4));

    CHECK(r[0] == 1);
    CHECK(r[1] == 2);
    CHECK(r[2] == 3);
    CHECK(r[3] == 4);
}

TEST_CASE("TaskMram handles file Write and Read correctly")
{
    // --------------------------
    // Setup
    // --------------------------
    RamMramMock<2048> mock;
    MramBackend<RamMramMock<2048>> backend(mock);

    std::tuple<> adapters;
    MockTaskMram<RamMramMock<2048>, MramLayout> task(backend, 0, 0, adapters);

    // --------------------------
    // 1. Construct a Write request
    // --------------------------
    uavcan_file_Write_Request_1_1 req{};
    const char* path = "config";
    std::memcpy(req.path.path.elements, path, 6);
    req.path.path.count = 6;

    req.offset = 0;
    req.data.value.count = 4;
    req.data.value.elements[0] = 10;
    req.data.value.elements[1] = 20;
    req.data.value.elements[2] = 30;
    req.data.value.elements[3] = 40;

    uint8_t write_payload[uavcan_file_Write_Request_1_1_EXTENT_BYTES_]{};
    size_t write_payload_size = sizeof(write_payload);
    REQUIRE(! uavcan_file_Write_Request_1_1_serialize_(&req, write_payload, &write_payload_size));

    MockTransfer write_transfer(uavcan_file_Write_1_1_FIXED_PORT_ID_, write_payload, write_payload_size);

    // --------------------------
    // 2. Execute Write
    // --------------------------
    task.handleWrite(write_transfer);

    // MRAM should now contain the 4 bytes at offset(config)
    uint32_t base = MramTraits<mram_config_t, MramLayout>::offset;
    uint8_t check[4]{};
    CHECK(mock.readBytes(base, check, 4));
    CHECK(check[0] == 10);
    CHECK(check[1] == 20);
    CHECK(check[2] == 30);
    CHECK(check[3] == 40);

    // --------------------------
    // 3. Construct a Read request
    // --------------------------
    uavcan_file_Read_Request_1_1 rreq{};
    std::memcpy(rreq.path.path.elements, path, 6);
    rreq.path.path.count = 6;
    rreq.offset = 0;

    uint8_t read_payload[uavcan_file_Read_Request_1_1_EXTENT_BYTES_]{};
    size_t read_payload_size = sizeof(read_payload);
    REQUIRE(! uavcan_file_Read_Request_1_1_serialize_(&rreq, read_payload, &read_payload_size));

    MockTransfer read_transfer(uavcan_file_Read_1_1_FIXED_PORT_ID_, read_payload, read_payload_size);

    // --------------------------
    // 4. Execute Read
    // --------------------------
    task.handleRead(read_transfer);

    // NOTE:
    // The response is published via TaskForServer::publish.
    // In a full test harness, you'd intercept publish() to inspect the response.
    // For now, verifying MRAM content + no crash is sufficient.
}
