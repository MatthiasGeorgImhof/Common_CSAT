#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "cyphal.hpp"
#include "cyphal_mtu.hpp"
#include "canard_adapter.hpp"
#include "Socket.hpp"
#include "TxQueueDrainer.hpp"
#include "Serialization.hpp"

#include <cstdint>
#include <cstring>
#include <vector>

void* canardMemoryAllocate(void* /*ins*/, size_t amount)
{ 
    return malloc(amount);
}

void canardMemoryFree(void* /*ins*/, size_t /*amount*/, void* pointer) 
{ 
    free(pointer); 
}

struct CanardMemoryResource canard_memory = { nullptr, canardMemoryFree, canardMemoryAllocate };

// =============================================================================
// BufTxQueueDrainer Tests
// =============================================================================

template <size_t N = 64>
struct MockRxFrame
{
    uint32_t extended_can_id{0};
    uint16_t size{0};
    uint8_t payload[N]{};
};

template <size_t N = 64>
class MockRingBuffer
{
public:
    MockRxFrame<N> staging{};
    std::vector<MockRxFrame<N>> committed_frames;

    MockRxFrame<N>& begin_write()
    {
        staging = {};
        return staging;
    }

    void commit_write()
    {
        committed_frames.push_back(staging);
    }
};

TEST_CASE("BufTxQueueDrainer drains single and multiple frames")
{
    constexpr CyphalPortID PORT_ID = 456;
    constexpr uint8_t MTU = SMALL_DATA_MTU;

    CanardAdapter adapter;
    adapter.ins = canardInit(canard_memory);
    adapter.ins.node_id = 15;
    adapter.que = canardTxInit(16, MTU, canard_memory);
    Cyphal<CanardAdapter> cyphal(&adapter);

    MockRingBuffer<MTU> buffer;
    BufTxQueueDrainer<MockRingBuffer<MTU>> drainer(&adapter, buffer);

    SUBCASE("Draining an empty queue does nothing")
    {
        drainer.drain();
        CHECK(buffer.committed_frames.empty());
        CHECK(canardTxPeek(&adapter.que) == nullptr);
    }

    SUBCASE("Drains single frame to buffer with matching ID and payload")
    {
        CyphalTransferMetadata metadata;
        metadata.priority = CyphalPriorityNominal;
        metadata.transfer_kind = CyphalTransferKindMessage;
        metadata.port_id = PORT_ID;
        metadata.remote_node_id = CYPHAL_NODE_ID_UNSET;
        metadata.remote_node_id = CYPHAL_NODE_ID_UNSET;
        metadata.source_node_id = CYPHAL_NODE_ID_UNSET;
        metadata.destination_node_id = CYPHAL_NODE_ID_UNSET;
        metadata.transfer_id = 0;

        const char payload[] = "data";
        REQUIRE(cyphal.cyphalTxPush(0, &metadata, sizeof(payload), payload) == 1);
        auto* peeked = canardTxPeek(&adapter.que);
        REQUIRE(peeked != nullptr);
        const uint32_t expected_can_id = peeked->frame.extended_can_id;

        drainer.drain();

        CHECK(canardTxPeek(&adapter.que) == nullptr);
        REQUIRE(buffer.committed_frames.size() == 1);
        CHECK(buffer.committed_frames[0].extended_can_id == expected_can_id);
        CHECK(buffer.committed_frames[0].size == sizeof(payload) + 1); // canard adds one byte to frame
        CHECK(memcmp(buffer.committed_frames[0].payload, payload, sizeof(payload)) == 0);
    }

    SUBCASE("Drains multi-frame transfer preserving order")
    {
        CyphalTransferMetadata metadata;
        metadata.priority = CyphalPriorityNominal;
        metadata.transfer_kind = CyphalTransferKindMessage;
        metadata.port_id = PORT_ID;
        metadata.remote_node_id = CYPHAL_NODE_ID_UNSET;
        metadata.remote_node_id = CYPHAL_NODE_ID_UNSET;
        metadata.source_node_id = CYPHAL_NODE_ID_UNSET;
        metadata.destination_node_id = CYPHAL_NODE_ID_UNSET;
        metadata.transfer_id = 2;

        const char payload[] = "a_somewhat_longer_payload_that_needs_multiple_frames";
        REQUIRE(cyphal.cyphalTxPush(0, &metadata, sizeof(payload), payload) >= 2);

        drainer.drain();

        CHECK(canardTxPeek(&adapter.que) == nullptr);
        CHECK(buffer.committed_frames.size() >= 2);
    }
}

// =============================================================================
// CanTxQueueDrainer Tests
// =============================================================================

#if !defined(__arm__) || defined(HAL_CAN_MODULE_ENABLED)

TEST_CASE("CanTxQueueDrainer drains to CAN mailboxes")
{
    constexpr CyphalPortID PORT_ID = 789;
    constexpr uint8_t MTU = SMALL_DATA_MTU;

    CanardAdapter adapter;
    adapter.ins = canardInit(canard_memory);
    adapter.ins.node_id = 22;
    adapter.que = canardTxInit(16, MTU, canard_memory);
    Cyphal<CanardAdapter> cyphal(&adapter);

    CAN_HandleTypeDef hcan{};
    CanTxQueueDrainer drainer(&adapter, &hcan);

    CyphalTransferMetadata metadata;
    metadata.priority = CyphalPriorityNominal;
    metadata.transfer_kind = CyphalTransferKindMessage;
    metadata.port_id = PORT_ID;
    metadata.remote_node_id = CYPHAL_NODE_ID_UNSET;
    metadata.remote_node_id = CYPHAL_NODE_ID_UNSET;
    metadata.source_node_id = CYPHAL_NODE_ID_UNSET;
    metadata.destination_node_id = CYPHAL_NODE_ID_UNSET;
    metadata.transfer_id = 0;

    const char payload[] = "cmsg";
    REQUIRE(cyphal.cyphalTxPush(0, &metadata, sizeof(payload), payload) == 1);

    SUBCASE("Drains queue when free mailboxes exist")
    {
        drainer.drain();
        CHECK(canardTxPeek(&adapter.que) == nullptr);
    }

    SUBCASE("irq_safe_drain works identically")
    {
        drainer.irq_safe_drain();
        CHECK(canardTxPeek(&adapter.que) == nullptr);
    }
}

#endif // !defined(__arm__) || defined(HAL_CAN_MODULE_ENABLED)

// =============================================================================
// SerialTxQueueDrainer Tests
// =============================================================================

#if !defined(__arm__) || defined(MOCK_HAL_UART_ENABLED)

TEST_CASE("SerialTxQueueDrainer encodes with COBS and transmits via UART")
{
    constexpr CyphalPortID PORT_ID = 321;
    constexpr uint8_t MTU = 64;

    CanardAdapter adapter;
    adapter.ins = canardInit(canard_memory);
    adapter.ins.node_id = 33;
    adapter.que = canardTxInit(16, MTU, canard_memory);
    Cyphal<CanardAdapter> cyphal(&adapter);

    UART_HandleTypeDef huart{};
    init_uart_handle(&huart);
    SerialTxQueueDrainer<MTU> drainer(&adapter, &huart);

    CyphalTransferMetadata metadata;
    metadata.priority = CyphalPriorityNominal;
    metadata.transfer_kind = CyphalTransferKindMessage;
    metadata.port_id = PORT_ID;
    metadata.remote_node_id = CYPHAL_NODE_ID_UNSET;
    metadata.remote_node_id = CYPHAL_NODE_ID_UNSET;
    metadata.source_node_id = CYPHAL_NODE_ID_UNSET;
    metadata.destination_node_id = CYPHAL_NODE_ID_UNSET;
    metadata.transfer_id = 0;

    const char payload[] = "serial_tx";
    REQUIRE(cyphal.cyphalTxPush(0, &metadata, sizeof(payload), payload) == 1);

    SUBCASE("Drains successfully when UART is not busy")
    {
        set_mocked_uart_state(HAL_UART_STATE_READY);
        clear_uart_tx_buffer();

        drainer.drain();

        CHECK(canardTxPeek(&adapter.que) == nullptr);
        CHECK(get_uart_tx_buffer_count() > 0);
    }

    SUBCASE("Halts drain when UART state is busy")
    {
        set_mocked_uart_state(HAL_UART_STATE_BUSY_TX);
        drainer.drain();
        
        // Frame must remain in queue since UART was busy
        CHECK(canardTxPeek(&adapter.que) != nullptr);

        // Clears once ready
        set_mocked_uart_state(HAL_UART_STATE_READY);
        drainer.drain();
        CHECK(canardTxPeek(&adapter.que) == nullptr);
    }
}

#endif // !defined(__arm__) || defined(MOCK_HAL_UART_ENABLED)

// =============================================================================
// UdpTxQueueDrainer Tests
// =============================================================================

class MockUdpDriver
{
public:
    enum SOCKET_NUMBER : uint8_t
    {
        SOCKET_0 = 0
    };

    bool openUdp(uint8_t, uint16_t) { return true; }
    void closeUdp(uint8_t) {}
    int32_t receiveUdp(uint8_t, uint8_t*, uint16_t, IPv4Address&, uint16_t&) { return 0; }
    uint8_t socketStatus(uint8_t) const { return 0; }
    uint16_t rxAvailable(uint8_t) const { return 0; }
    uint16_t txAvailable(uint8_t) const { return 2048; }

    uint32_t send_count{0};
    uint8_t last_socket{};
    uint16_t last_size{};
    IPv4Address last_ip{};
    uint16_t last_port{};
    std::vector<uint8_t> last_payload;

    int32_t sendUdp(uint8_t socket, const uint8_t* data, uint16_t size, const IPv4Address& address, uint16_t port)
    {
        ++send_count;
        last_socket = socket;
        last_size = size;
        last_ip = address;
        last_port = port;
        last_payload.assign(data, data + size);
        return static_cast<int32_t>(size);
    }
};

inline constexpr Endpoint TEST_ENDPOINT{ IPv4Address{192, 168, 1, 50}, 9382 };

TEST_CASE("UdpTxQueueDrainer drains single Cyphal frame")
{
    constexpr CyphalPortID PORT_ID = 123;
    constexpr uint8_t MTU = 64;

    CanardAdapter adapter;
    adapter.ins = canardInit(canard_memory);
    adapter.ins.node_id = 11;
    adapter.que = canardTxInit(16, MTU, canard_memory);
    Cyphal<CanardAdapter> cyphal(&adapter);

    CyphalTransferMetadata metadata;
    metadata.priority = CyphalPriorityNominal;
    metadata.transfer_kind = CyphalTransferKindMessage;
    metadata.port_id = PORT_ID;
    metadata.remote_node_id = CYPHAL_NODE_ID_UNSET;
    metadata.remote_node_id = CYPHAL_NODE_ID_UNSET;
    metadata.source_node_id = CYPHAL_NODE_ID_UNSET;
    metadata.destination_node_id = CYPHAL_NODE_ID_UNSET;
    metadata.transfer_id = 0;

    const char payload[] = "hello";
    REQUIRE(cyphal.cyphalTxPush(0, &metadata, sizeof(payload), payload) == 1);
    REQUIRE(canardTxPeek(&adapter.que) != nullptr);

    MockUdpDriver driver;
    UdpSocket<MockUdpDriver> socket(driver, MockUdpDriver::SOCKET_0);
    UdpTxQueueDrainer<UdpSocket<MockUdpDriver>, TEST_ENDPOINT, MTU> drainer(&adapter, &socket);

    drainer.drain();

    CHECK(driver.send_count == 1);
    CHECK(driver.last_socket == MockUdpDriver::SOCKET_0);
    CHECK(driver.last_port == TEST_ENDPOINT.port);
    CHECK(memcmp(driver.last_ip.bytes, TEST_ENDPOINT.address.bytes, sizeof(TEST_ENDPOINT.address.bytes)) == 0);
    CHECK(canardTxPeek(&adapter.que) == nullptr);
}

TEST_CASE("UdpTxQueueDrainer transmits correct serialized frame")
{
    constexpr CyphalPortID PORT_ID = 123;
    constexpr uint8_t MTU = 64;

    CanardAdapter adapter;
    adapter.ins = canardInit(canard_memory);
    adapter.ins.node_id = 11;
    adapter.que = canardTxInit(16, MTU, canard_memory);
    Cyphal<CanardAdapter> cyphal(&adapter);

    CyphalTransferMetadata metadata;
    metadata.priority = CyphalPriorityNominal;
    metadata.transfer_kind = CyphalTransferKindMessage;
    metadata.port_id = PORT_ID;
    metadata.remote_node_id = CYPHAL_NODE_ID_UNSET;
    metadata.remote_node_id = CYPHAL_NODE_ID_UNSET;
    metadata.source_node_id = CYPHAL_NODE_ID_UNSET;
    metadata.destination_node_id = CYPHAL_NODE_ID_UNSET;
    metadata.transfer_id = 0;

    const char payload[] = "testing123";
    REQUIRE(cyphal.cyphalTxPush(0, &metadata, sizeof(payload), payload) == 1);
    auto* item = canardTxPeek(&adapter.que);
    REQUIRE(item != nullptr);

    PlainFramer<MTU> framer;
    auto expected = framer.serialize(item->frame);

    MockUdpDriver driver;
    UdpSocket<MockUdpDriver> socket(driver, MockUdpDriver::SOCKET_0);
    UdpTxQueueDrainer<UdpSocket<MockUdpDriver>, TEST_ENDPOINT, MTU> drainer(&adapter, &socket);
    drainer.drain();

    REQUIRE(driver.send_count == 1);
    REQUIRE(driver.last_payload.size() == sizeof(expected));
    CHECK(memcmp(driver.last_payload.data(), &expected, serialized_size(expected)) == 0);
}

TEST_CASE("UdpTxQueueDrainer drains all queued frames")
{
    constexpr CyphalPortID PORT_ID = 123;
    constexpr uint8_t MTU = SMALL_DATA_MTU;

    CanardAdapter adapter;
    adapter.ins = canardInit(canard_memory);
    adapter.ins.node_id = 11;
    adapter.que = canardTxInit(16, MTU, canard_memory);
    Cyphal<CanardAdapter> cyphal(&adapter);

    CyphalTransferMetadata metadata;
    metadata.priority = CyphalPriorityNominal;
    metadata.transfer_kind = CyphalTransferKindMessage;
    metadata.port_id = PORT_ID;
    metadata.remote_node_id = CYPHAL_NODE_ID_UNSET;
    metadata.remote_node_id = CYPHAL_NODE_ID_UNSET;
    metadata.source_node_id = CYPHAL_NODE_ID_UNSET;
    metadata.destination_node_id = CYPHAL_NODE_ID_UNSET;
    metadata.transfer_id = 0;

    const char payload[] = "hello_hello_hello_hello_hello_hello_hello";

    const auto queued_before = canardTxPeek(&adapter.que);
    CHECK(queued_before == nullptr);

    REQUIRE(cyphal.cyphalTxPush(0, &metadata, sizeof(payload), payload) >= 2);

    MockUdpDriver driver;
    UdpSocket<MockUdpDriver> socket(driver, MockUdpDriver::SOCKET_0);
    UdpTxQueueDrainer<UdpSocket<MockUdpDriver>, TEST_ENDPOINT, MTU> drainer(&adapter, &socket);
    drainer.drain();

    CHECK(driver.send_count >= 2);
    CHECK(canardTxPeek(&adapter.que) == nullptr);
}