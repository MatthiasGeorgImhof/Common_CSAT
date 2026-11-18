#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>

#include "RecordRegistry.hpp"
#include "Register.hpp"
#include "StorageBackend.hpp"
#include "RecordStore.hpp"
#include "RamBufferBackend.hpp"
#include "RamStorageMock.hpp"
#include "TaskRespondRegisterAccess.hpp"
#include "Task.hpp"
#include "cyphal.hpp"
#include "loopard_adapter.hpp"
#include "RegistrationManager.hpp"
#include "ServiceManager.hpp"

#include "uavcan/_register/Access_1_0.h"

#ifdef __x86_64__
#include "mock_hal.h"
#endif

void* loopardMemoryAllocate(size_t amount) { return std::malloc(amount); }
void loopardMemoryFree(void* pointer) { std::free(pointer); }

// ------------------------------------------------------------
// Test MRAM layout & register codecs
// ------------------------------------------------------------
struct reg_config_t
{
    std::uint32_t baudrate;
    std::uint32_t node_id;
};

struct reg_gain_t
{
    float kp;
};

struct reg_blob_t
{
    std::uint8_t data[6];
};

struct TestAccessLayout
{
    StorageRecord<reg_config_t> config;
    StorageRecord<reg_gain_t>   gain;
    StorageRecord<reg_blob_t>   blob;
};

template <>
struct LayoutMembers<TestAccessLayout>
{
    static constexpr auto members = std::make_tuple(
        &TestAccessLayout::config,
        &TestAccessLayout::gain,
        &TestAccessLayout::blob
    );
};

template <>
struct RecordTraits<reg_config_t, TestAccessLayout>
{
    static constexpr std::uint32_t offset  = offsetof(TestAccessLayout, config);
    static constexpr std::uint32_t version = 1;
    static constexpr std::size_t   index   = 0;
    static constexpr const char*   name    = "can.config";
};

template <>
struct RecordTraits<reg_gain_t, TestAccessLayout>
{
    static constexpr std::uint32_t offset  = offsetof(TestAccessLayout, gain);
    static constexpr std::uint32_t version = 1;
    static constexpr std::size_t   index   = 1;
    static constexpr const char*   name    = "ctrl.kp";
};

template <>
struct RecordTraits<reg_blob_t, TestAccessLayout>
{
    static constexpr std::uint32_t offset  = offsetof(TestAccessLayout, blob);
    static constexpr std::uint32_t version = 1;
    static constexpr std::size_t   index   = 2;
    static constexpr const char*   name    = "device.uid";
};

// Generic unstructured fallback codec
template <typename T>
struct RegisterCodec<T, uavcan_register_Value_1_0>
{
    static_assert(std::is_trivially_copyable_v<T>);
    static_assert(sizeof(T) <= uavcan_primitive_Unstructured_1_0_value_ARRAY_CAPACITY_);

    static bool encode(const T& payload, uavcan_register_Value_1_0& out)
    {
        uavcan_register_Value_1_0_select_unstructured_(&out);
        std::memcpy(out.unstructured.value.elements, &payload, sizeof(T));
        out.unstructured.value.count = sizeof(T);
        return true;
    }

    static bool decode(const uavcan_register_Value_1_0& in, T& payload)
    {
        if (!uavcan_register_Value_1_0_is_unstructured_(&in) ||
            in.unstructured.value.count != sizeof(T))
            return false;

        std::memcpy(&payload, in.unstructured.value.elements, sizeof(T));
        return true;
    }
};

// Typed natural32[2] codec for reg_config_t
template <>
struct RegisterCodec<reg_config_t, uavcan_register_Value_1_0>
{
    static bool encode(const reg_config_t& payload, uavcan_register_Value_1_0& out)
    {
        uavcan_register_Value_1_0_select_natural32_(&out);
        out.natural32.value.elements[0] = payload.baudrate;
        out.natural32.value.elements[1] = payload.node_id;
        out.natural32.value.count = 2;
        return true;
    }

    static bool decode(const uavcan_register_Value_1_0& in, reg_config_t& payload)
    {
        if (!uavcan_register_Value_1_0_is_natural32_(&in) ||
            in.natural32.value.count != 2)
            return false;
        if (in.natural32.value.elements[1] > 127)
            return false;

        payload.baudrate = in.natural32.value.elements[0];
        payload.node_id  = in.natural32.value.elements[1];
        return true;
    }
};

// Typed real32[1] codec for reg_gain_t
template <>
struct RegisterCodec<reg_gain_t, uavcan_register_Value_1_0>
{
    static bool encode(const reg_gain_t& payload, uavcan_register_Value_1_0& out)
    {
        uavcan_register_Value_1_0_select_real32_(&out);
        out.real32.value.elements[0] = payload.kp;
        out.real32.value.count = 1;
        return true;
    }

    static bool decode(const uavcan_register_Value_1_0& in, reg_gain_t& payload)
    {
        if (!uavcan_register_Value_1_0_is_real32_(&in) ||
            in.real32.value.count != 1)
            return false;

        payload.kp = in.real32.value.elements[0];
        return true;
    }
};

// ------------------------------------------------------------
// Helpers
// ------------------------------------------------------------
std::shared_ptr<CyphalTransfer> createAccessRequest(std::string_view name,
                                                    const uavcan_register_Value_1_0& value,
                                                    std::uint8_t transfer_id = 0)
{
    auto transfer = std::make_shared<CyphalTransfer>();
    transfer->metadata.transfer_kind  = CyphalTransferKindRequest;
    transfer->metadata.port_id        = uavcan_register_Access_1_0_FIXED_PORT_ID_;
    transfer->metadata.remote_node_id = 123;
    transfer->metadata.transfer_id    = transfer_id;

    uavcan_register_Access_Request_1_0 request{};
    const std::size_t copy_len = std::min(name.size(), sizeof(request.name.name.elements));
    request.name.name.count = copy_len;
    if (copy_len > 0)
        std::memcpy(request.name.name.elements, name.data(), copy_len);
    request.value = value;

    transfer->payload_size = uavcan_register_Access_Request_1_0_SERIALIZATION_BUFFER_SIZE_BYTES_;
    transfer->payload = loopardMemoryAllocate(transfer->payload_size);
    REQUIRE(transfer->payload != nullptr);

    const int8_t res = uavcan_register_Access_Request_1_0_serialize_(
        &request,
        static_cast<std::uint8_t*>(transfer->payload),
        &transfer->payload_size);
    REQUIRE(res >= 0);

    return transfer;
}

uavcan_register_Access_Response_1_0 decodeAccessResponse(CyphalTransfer& response)
{
    CHECK(response.metadata.port_id == uavcan_register_Access_1_0_FIXED_PORT_ID_);
    CHECK(response.metadata.transfer_kind == CyphalTransferKindResponse);
    CHECK(response.payload_size <= uavcan_register_Access_Response_1_0_SERIALIZATION_BUFFER_SIZE_BYTES_);

    uavcan_register_Access_Response_1_0 received{};
    size_t deserialized_size = response.payload_size;
    const int8_t res = uavcan_register_Access_Response_1_0_deserialize_(
        &received,
        static_cast<const std::uint8_t*>(response.payload),
        &deserialized_size);
    REQUIRE(res >= 0);

    loopardMemoryFree(response.payload);
    return received;
}

template <typename Registry, typename Store, typename... Adapters>
class MockTaskRespondRegisterAccess : public TaskRespondRegisterAccess<Registry, Store, Adapters...>
{
public:
    MockTaskRespondRegisterAccess(Registry& registry,
                                  Store& store,
                                  uint32_t interval,
                                  uint32_t tick,
                                  std::tuple<Adapters...>& adapters)
        : TaskRespondRegisterAccess<Registry, Store, Adapters...>(registry, store, interval, tick, adapters) {}

    using TaskRespondRegisterAccess<Registry, Store, Adapters...>::handleMessage;
    size_t getBufferSize() const { return TaskRespondRegisterAccess<Registry, Store, Adapters...>::buffer_.size(); }
    using TaskRespondRegisterAccess<Registry, Store, Adapters...>::buffer_;
};

// // ------------------------------------------------------------
// // Tests
// // ------------------------------------------------------------
// TEST_CASE("TaskRespondRegisterAccess: Get on erased MRAM returns empty value with mutable/persistent flags")
// {
//     LoopardAdapter loopard1, loopard2;
//     loopard1.memory_allocate = loopardMemoryAllocate;
//     loopard1.memory_free     = loopardMemoryFree;
//     loopard2.memory_allocate = loopardMemoryAllocate;
//     loopard2.memory_free     = loopardMemoryFree;

//     Cyphal<LoopardAdapter> loopard_cyphal1(&loopard1), loopard_cyphal2(&loopard2);
//     loopard_cyphal1.setNodeID(11);
//     loopard_cyphal2.setNodeID(12);
//     std::tuple<Cyphal<LoopardAdapter>, Cyphal<LoopardAdapter>> adapters(loopard_cyphal1, loopard_cyphal2);

//     RamStorageMock<sizeof(TestAccessLayout)> mock;
//     RamMockBackend backend(mock);
//     RecordStore<TestAccessLayout, decltype(backend)> store(backend);
//     RegisterTable<TestAccessLayout, uavcan_register_Value_1_0> registry;

//     MockTaskRespondRegisterAccess task(registry, store, 1000, 0, adapters);

//     uavcan_register_Value_1_0 empty_val{};
//     uavcan_register_Value_1_0_select_empty_(&empty_val);

//     auto req = createAccessRequest("can.config", empty_val, 1);
//     task.handleMessage(req);
//     REQUIRE(task.getBufferSize() == 1);

//     task.handleTaskImpl();
//     loopardMemoryFree(req->payload);

//     REQUIRE(loopard1.buffer.size() == 1);
//     REQUIRE(loopard2.buffer.size() == 1);

//     auto transfer1 = loopard1.buffer.pop();
//     auto transfer2 = loopard2.buffer.pop();
//     auto resp1 = decodeAccessResponse(transfer1);
//     auto resp2 = decodeAccessResponse(transfer2);

//     CHECK(resp1._mutable == true);
//     CHECK(resp1.persistent == true);
//     CHECK(uavcan_register_Value_1_0_is_empty_(&resp1.value));

//     CHECK(resp2._mutable == true);
//     CHECK(resp2.persistent == true);
//     CHECK(uavcan_register_Value_1_0_is_empty_(&resp2.value));
// }

TEST_CASE("TaskRespondRegisterAccess: Basic Get")
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

    RamStorageMock<sizeof(TestAccessLayout)> mock;
    RamMockBackend backend(mock);
    RecordStore<TestAccessLayout, decltype(backend)> store(backend);
    RegisterTable<TestAccessLayout, uavcan_register_Value_1_0> registry;

    REQUIRE(store.set(reg_gain_t{3.5f}) == true);

    MockTaskRespondRegisterAccess task(registry, store, 1000, 0, adapters);

    uavcan_register_Value_1_0 empty_val{};
    uavcan_register_Value_1_0_select_empty_(&empty_val);

    auto req = createAccessRequest("ctrl.kp", empty_val, 1);
    task.handleMessage(req);
    REQUIRE(task.getBufferSize() == 1);

    task.handleTaskImpl();
    loopardMemoryFree(req->payload);

    REQUIRE(loopard1.buffer.size() == 1);
    REQUIRE(loopard2.buffer.size() == 1);

    auto transfer1 = loopard1.buffer.pop();
    auto transfer2 = loopard2.buffer.pop();
    auto resp1 = decodeAccessResponse(transfer1);
    auto resp2 = decodeAccessResponse(transfer2);

    CHECK(resp1._mutable == true);
    CHECK(resp1.persistent == true);
    REQUIRE(uavcan_register_Value_1_0_is_real32_(&resp1.value));
    REQUIRE(resp1.value.real32.value.count == 1);
    CHECK(resp1.value.real32.value.elements[0] == doctest::Approx(3.5f));

    CHECK(resp2._mutable == true);
    CHECK(resp2.persistent == true);
    REQUIRE(uavcan_register_Value_1_0_is_real32_(&resp2.value));
    REQUIRE(resp2.value.real32.value.count == 1);
    CHECK(resp2.value.real32.value.elements[0] == doctest::Approx(3.5f));
}

// TEST_CASE("TaskRespondRegisterAccess: Set assigns value, responds with retrieved value, and updates RecordStore")
// {
//     LoopardAdapter loopard1, loopard2;
//     loopard1.memory_allocate = loopardMemoryAllocate;
//     loopard1.memory_free     = loopardMemoryFree;
//     loopard2.memory_allocate = loopardMemoryAllocate;
//     loopard2.memory_free     = loopardMemoryFree;

//     Cyphal<LoopardAdapter> loopard_cyphal1(&loopard1), loopard_cyphal2(&loopard2);
//     loopard_cyphal1.setNodeID(11);
//     loopard_cyphal2.setNodeID(12);
//     std::tuple<Cyphal<LoopardAdapter>, Cyphal<LoopardAdapter>> adapters(loopard_cyphal1, loopard_cyphal2);

//     RamStorageMock<sizeof(TestAccessLayout)> mock;
//     RamMockBackend backend(mock);
//     RecordStore<TestAccessLayout, decltype(backend)> store(backend);
//     RegisterTable<TestAccessLayout, uavcan_register_Value_1_0> registry;

//     MockTaskRespondRegisterAccess task(registry, store, 1000, 0, adapters);

//     auto send_and_receive = [&](std::string_view name,
//                                 const uavcan_register_Value_1_0& val,
//                                 std::uint8_t tid) {
//         auto req = createAccessRequest(name, val, tid);
//         task.handleMessage(req);
//         task.handleTaskImpl();
//         loopardMemoryFree(req->payload);

//         REQUIRE(loopard1.buffer.size() == 1);
//         REQUIRE(loopard2.buffer.size() == 1);
//         auto transfer1 = loopard1.buffer.pop();
//         auto transfer2 = loopard2.buffer.pop();
//         auto resp1 = decodeAccessResponse(transfer1);
//         auto resp2 = decodeAccessResponse(transfer2);
//         CHECK(resp1._mutable == resp2._mutable);
//         CHECK(resp1.persistent == resp2.persistent);
//         return resp1;
//     };

//     // 1. Set "can.config" (natural32)
//     uavcan_register_Value_1_0 set_cfg{};
//     uavcan_register_Value_1_0_select_natural32_(&set_cfg);
//     set_cfg.natural32.value.elements[0] = 1000000;
//     set_cfg.natural32.value.elements[1] = 42;
//     set_cfg.natural32.value.count = 2;

//     auto set_resp = send_and_receive("can.config", set_cfg, 1);
//     CHECK(set_resp._mutable == true);
//     CHECK(set_resp.persistent == true);
//     REQUIRE(uavcan_register_Value_1_0_is_natural32_(&set_resp.value));
//     REQUIRE(set_resp.value.natural32.value.count == 2);
//     CHECK(set_resp.value.natural32.value.elements[0] == 1000000);
//     CHECK(set_resp.value.natural32.value.elements[1] == 42);

//     // Verify RecordStore sees valid CRC, version, and seq == 1
//     StorageRecord<reg_config_t> rec{};
//     REQUIRE(store.get_record(rec) == true);
//     CHECK(rec.seq == 1);
//     CHECK(rec.payload.baudrate == 1000000);
//     CHECK(rec.payload.node_id == 42);

//     // 2. Get "can.config" with empty request
//     uavcan_register_Value_1_0 empty_val{};
//     uavcan_register_Value_1_0_select_empty_(&empty_val);
//     auto get_resp = send_and_receive("can.config", empty_val, 2);
//     REQUIRE(uavcan_register_Value_1_0_is_natural32_(&get_resp.value));
//     CHECK(get_resp.value.natural32.value.elements[0] == 1000000);
//     CHECK(get_resp.value.natural32.value.elements[1] == 42);

//     // 3. Second Set increments seq to 2
//     set_cfg.natural32.value.elements[0] = 500000;
//     set_cfg.natural32.value.elements[1] = 7;
//     auto set_resp2 = send_and_receive("can.config", set_cfg, 3);
//     CHECK(set_resp2.value.natural32.value.elements[0] == 500000);
//     CHECK(set_resp2.value.natural32.value.elements[1] == 7);
//     REQUIRE(store.get_record(rec) == true);
//     CHECK(rec.seq == 2);

//     // 4. Set "ctrl.kp" (real32)
//     uavcan_register_Value_1_0 set_gain{};
//     uavcan_register_Value_1_0_select_real32_(&set_gain);
//     set_gain.real32.value.elements[0] = 3.5f;
//     set_gain.real32.value.count = 1;
//     auto gain_resp = send_and_receive("ctrl.kp", set_gain, 4);
//     REQUIRE(uavcan_register_Value_1_0_is_real32_(&gain_resp.value));
//     REQUIRE(gain_resp.value.real32.value.count == 1);
//     CHECK(gain_resp.value.real32.value.elements[0] == doctest::Approx(3.5f));

//     // 5. Set "device.uid" (unstructured)
//     uavcan_register_Value_1_0 set_blob{};
//     uavcan_register_Value_1_0_select_unstructured_(&set_blob);
//     const std::uint8_t uid[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02};
//     std::memcpy(set_blob.unstructured.value.elements, uid, sizeof(uid));
//     set_blob.unstructured.value.count = sizeof(uid);

//     auto blob_resp = send_and_receive("device.uid", set_blob, 5);
//     REQUIRE(uavcan_register_Value_1_0_is_unstructured_(&blob_resp.value));
//     REQUIRE(blob_resp.value.unstructured.value.count == sizeof(uid));
//     CHECK(std::memcmp(blob_resp.value.unstructured.value.elements, uid, sizeof(uid)) == 0);
// }

// TEST_CASE("TaskRespondRegisterAccess: invalid Set preserves existing value and unknown register returns empty")
// {
//     LoopardAdapter loopard1, loopard2;
//     loopard1.memory_allocate = loopardMemoryAllocate;
//     loopard1.memory_free     = loopardMemoryFree;
//     loopard2.memory_allocate = loopardMemoryAllocate;
//     loopard2.memory_free     = loopardMemoryFree;

//     Cyphal<LoopardAdapter> loopard_cyphal1(&loopard1), loopard_cyphal2(&loopard2);
//     loopard_cyphal1.setNodeID(11);
//     loopard_cyphal2.setNodeID(12);
//     std::tuple<Cyphal<LoopardAdapter>, Cyphal<LoopardAdapter>> adapters(loopard_cyphal1, loopard_cyphal2);

//     RamStorageMock<sizeof(TestAccessLayout)> mock;
//     RamMockBackend backend(mock);
//     RecordStore<TestAccessLayout, decltype(backend)> store(backend);
//     RegisterTable<TestAccessLayout, uavcan_register_Value_1_0> registry;

//     REQUIRE(store.set(reg_config_t{250000, 10}) == true);

//     MockTaskRespondRegisterAccess task(registry, store, 1000, 0, adapters);

//     SUBCASE("Wrong value type does not overwrite register")
//     {
//         uavcan_register_Value_1_0 wrong_type{};
//         uavcan_register_Value_1_0_select_real32_(&wrong_type);
//         wrong_type.real32.value.elements[0] = 9.9f;
//         wrong_type.real32.value.count = 1;

//         auto req = createAccessRequest("can.config", wrong_type, 1);
//         task.handleMessage(req);
//         task.handleTaskImpl();
//         loopardMemoryFree(req->payload);

//         auto transfer1 = loopard1.buffer.pop();
//         auto transfer2 = loopard2.buffer.pop();
//         auto resp1 = decodeAccessResponse(transfer1);
//         auto resp2 = decodeAccessResponse(transfer2);
//         (void)resp2;

//         REQUIRE(uavcan_register_Value_1_0_is_natural32_(&resp1.value));
//         CHECK(resp1.value.natural32.value.elements[0] == 250000);
//         CHECK(resp1.value.natural32.value.elements[1] == 10);
//     }

//     SUBCASE("Semantic decode failure (node_id > 127) does not overwrite register")
//     {
//         uavcan_register_Value_1_0 bad_range{};
//         uavcan_register_Value_1_0_select_natural32_(&bad_range);
//         bad_range.natural32.value.elements[0] = 500000;
//         bad_range.natural32.value.elements[1] = 200; // > 127
//         bad_range.natural32.value.count = 2;

//         auto req = createAccessRequest("can.config", bad_range, 2);
//         task.handleMessage(req);
//         task.handleTaskImpl();
//         loopardMemoryFree(req->payload);

//         auto transfer1 = loopard1.buffer.pop();
//         auto transfer2 = loopard2.buffer.pop();
//         auto resp1 = decodeAccessResponse(transfer1);
//         auto resp2 = decodeAccessResponse(transfer2);
//         (void)resp2;

//         REQUIRE(uavcan_register_Value_1_0_is_natural32_(&resp1.value));
//         CHECK(resp1.value.natural32.value.elements[0] == 250000);
//         CHECK(resp1.value.natural32.value.elements[1] == 10);
//     }

//     SUBCASE("Unknown register returns empty, mutable=false, persistent=false")
//     {
//         uavcan_register_Value_1_0 empty_val{};
//         uavcan_register_Value_1_0_select_empty_(&empty_val);

//         auto req = createAccessRequest("does.not.exist", empty_val, 3);
//         task.handleMessage(req);
//         task.handleTaskImpl();
//         loopardMemoryFree(req->payload);

//         auto transfer1 = loopard1.buffer.pop();
//         auto transfer2 = loopard2.buffer.pop();
//         auto resp1 = decodeAccessResponse(transfer1);
//         auto resp2 = decodeAccessResponse(transfer2);

//         CHECK(resp1._mutable == false);
//         CHECK(resp1.persistent == false);
//         CHECK(uavcan_register_Value_1_0_is_empty_(&resp1.value));

//         CHECK(resp2._mutable == false);
//         CHECK(resp2.persistent == false);
//         CHECK(uavcan_register_Value_1_0_is_empty_(&resp2.value));
//     }
// }

// TEST_CASE("TaskRespondRegisterAccess: registers and unregisters correctly")
// {
//     RegistrationManager registration_manager;

//     LoopardAdapter loopard1, loopard2;
//     loopard1.memory_allocate = loopardMemoryAllocate;
//     loopard1.memory_free     = loopardMemoryFree;
//     loopard2.memory_allocate = loopardMemoryAllocate;
//     loopard2.memory_free     = loopardMemoryFree;

//     Cyphal<LoopardAdapter> loopard_cyphal1(&loopard1), loopard_cyphal2(&loopard2);
//     loopard_cyphal1.setNodeID(11);
//     loopard_cyphal2.setNodeID(12);
//     std::tuple<Cyphal<LoopardAdapter>, Cyphal<LoopardAdapter>> adapters(loopard_cyphal1, loopard_cyphal2);

//     RamStorageMock<sizeof(TestAccessLayout)> mock;
//     RamMockBackend backend(mock);
//     RecordStore<TestAccessLayout, decltype(backend)> store(backend);
//     RegisterTable<TestAccessLayout, uavcan_register_Value_1_0> registry;

//     auto task = std::make_shared<TaskRespondRegisterAccess<
//         decltype(registry),
//         decltype(store),
//         Cyphal<LoopardAdapter>,
//         Cyphal<LoopardAdapter>>>(registry, store, 1000, 0, adapters);

//     CHECK(registration_manager.getServers().size() == 0);

//     task->registerTask(&registration_manager, task);
//     CHECK(registration_manager.getServers().size() == 1);
//     CHECK(registration_manager.getServers().containsIf([&](const CyphalPortID& port_id) {
//         return port_id == uavcan_register_Access_1_0_FIXED_PORT_ID_;
//     }));

//     task->unregisterTask(&registration_manager, task);
//     CHECK(registration_manager.getServers().size() == 0);
// }