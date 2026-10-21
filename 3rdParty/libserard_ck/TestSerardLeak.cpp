#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

extern "C" {
#include "serard.h"
}

#include <cstdlib>
#include <vector>

// ---------------- Memory resources with counters ----------------

extern "C" {

static size_t g_payload_allocs = 0;
static size_t g_payload_frees  = 0;

static size_t g_session_allocs = 0;
static size_t g_session_frees  = 0;

static void* test_alloc_payload(void* user, size_t size)
{
    (void)user;
    g_payload_allocs++;
    return malloc(size);
}

static void test_free_payload(void* user, size_t size, void* ptr)
{
    (void)user;
    (void)size;
    g_payload_frees++;
    free(ptr);
}

static void* test_alloc_session(void* user, size_t size)
{
    (void)user;
    g_session_allocs++;
    return malloc(size);
}

static void test_free_session(void* user, size_t size, void* ptr)
{
    (void)user;
    (void)size;
    g_session_frees++;
    free(ptr);
}

} // extern "C"

// ---------------- Helpers ----------------

static SerardRx make_instance()
{
    SerardMemoryResource payload_mem{};
    payload_mem.user_reference = nullptr;
    payload_mem.allocate       = test_alloc_payload;
    payload_mem.deallocate     = test_free_payload;

    SerardMemoryResource session_mem{};
    session_mem.user_reference = nullptr;
    session_mem.allocate       = test_alloc_session;
    session_mem.deallocate     = test_free_session;

    SerardRx ins = serardInit(payload_mem, session_mem);
    ins.node_id          = SERARD_NODE_ID_UNSET;
    ins.forward_start_id = 0;
    ins.forward_end_id   = 0;
    return ins;
}

// TX emitter: collect fragments into a buffer
struct TxBuffer
{
    std::vector<uint8_t> data;
};

static bool tx_emitter(void* user, uint8_t size, const uint8_t* bytes)
{
    TxBuffer* buf = reinterpret_cast<TxBuffer*>(user);
    buf->data.insert(buf->data.end(), bytes, bytes + size);
    return true;
}

// ---------------- Tests ----------------

// Anonymous source: no RX sessions should be allocated.
TEST_CASE("rxAcceptTransfer with anonymous source does not allocate sessions")
{
    g_payload_allocs = g_payload_frees = 0;
    g_session_allocs = g_session_frees = 0;

    SerardRx rx = make_instance();

    SerardRxSubscription sub{};
    REQUIRE(serardRxSubscribe(&rx,
                              SerardTransferKindMessage,
                              123,
                              64,
                              SERARD_DEFAULT_TRANSFER_ID_TIMEOUT_USEC,
                              &sub) >= 0);

    uint8_t dummy_payload[1] = {0xAA};

    SerardTransferMetadata md{};
    md.priority       = SerardPriorityNominal;
    md.transfer_kind  = SerardTransferKindMessage;
    md.port_id        = 123;
    md.remote_node_id = SERARD_NODE_ID_UNSET;   // message → remote must be unset
    md.transfer_id    = 0;

    TxBuffer txbuf;
    REQUIRE(serardTxPush(SERARD_NODE_ID_UNSET,
                         &md,
                         sizeof(dummy_payload),
                         dummy_payload,
                         &txbuf,
                         tx_emitter) > 0);

    SerardReassembler reas = serardReassemblerInit();

    constexpr int N = 50;
    for (int i = 0; i < N; i++)
    {
        size_t remaining    = txbuf.data.size();
        const uint8_t* ptr  = txbuf.data.data();

        while (remaining > 0)
        {
            size_t chunk = remaining;
            SerardRxTransfer out{};
            SerardRxSubscription* out_sub = nullptr;

            int8_t res = serardRxAccept(&rx,
                                        &reas,
                                        /*timestamp*/ 1000 + i,
                                        &chunk,
                                        ptr,
                                        /*redundant_transport_index*/ 0,
                                        &out,
                                        &out_sub);

            if (out.payload)
            {
                rx.memory_payload.deallocate(rx.memory_payload.user_reference,
                                             out.payload_extent,
                                             out.payload);
            }

            ptr       += chunk;
            remaining -= chunk;

            if (res != 0)
                break;
        }
    }

    CHECK(g_session_allocs == 0);
    CHECK(g_session_frees == 0);
}

// Non-anonymous source: exactly one RX session should be allocated and reused.
TEST_CASE("rxAcceptTransfer from node 11 repeatedly allocates a single session")
{
    g_payload_allocs = g_payload_frees = 0;
    g_session_allocs = g_session_frees = 0;

    SerardRx rx = make_instance();

    SerardRxSubscription sub{};
    REQUIRE(serardRxSubscribe(&rx,
                              SerardTransferKindMessage,
                              123,
                              64,
                              SERARD_DEFAULT_TRANSFER_ID_TIMEOUT_USEC,
                              &sub) >= 0);

    uint8_t dummy_payload[1] = {0xAA};

    SerardTransferMetadata md{};
    md.priority       = SerardPriorityNominal;
    md.transfer_kind  = SerardTransferKindMessage;
    md.port_id        = 123;
    md.remote_node_id = SERARD_NODE_ID_UNSET;   // message → remote must be unset
    md.transfer_id    = 0;

    TxBuffer txbuf;
    // Source node 11 (non-anonymous) so RX should create a session for node 11.
    REQUIRE(serardTxPush(11,
                         &md,
                         sizeof(dummy_payload),
                         dummy_payload,
                         &txbuf,
                         tx_emitter) > 0);

    SerardReassembler reas = serardReassemblerInit();

    constexpr int N = 50;
    for (int i = 0; i < N; i++)
    {
        size_t remaining    = txbuf.data.size();
        const uint8_t* ptr  = txbuf.data.data();

        while (remaining > 0)
        {
            size_t chunk = remaining;
            SerardRxTransfer out{};
            SerardRxSubscription* out_sub = nullptr;

            int8_t res = serardRxAccept(&rx,
                                        &reas,
                                        /*timestamp*/ 1000 + i,
                                        &chunk,
                                        ptr,
                                        /*redundant_transport_index*/ 0,
                                        &out,
                                        &out_sub);

            if (out.payload)
            {
                rx.memory_payload.deallocate(rx.memory_payload.user_reference,
                                             out.payload_extent,
                                             out.payload);
            }

            ptr       += chunk;
            remaining -= chunk;

            if (res != 0)
                break;
        }
    }

    // Expected correct behavior: one session allocated and kept.
    CHECK(g_session_allocs == 1);
    CHECK(g_session_frees == 0);
}

// Non-anonymous source: exactly one RX session should be allocated and reused.
TEST_CASE("rxAcceptTransfer from different nodes repeatedly allocate a single session")
{
    g_payload_allocs = g_payload_frees = 0;
    g_session_allocs = g_session_frees = 0;

    SerardRx rx = make_instance();

    SerardRxSubscription sub{};
    REQUIRE(serardRxSubscribe(&rx,
                              SerardTransferKindMessage,
                              123,
                              64,
                              SERARD_DEFAULT_TRANSFER_ID_TIMEOUT_USEC,
                              &sub) >= 0);

    uint8_t dummy_payload[1] = {0xAA};

    SerardTransferMetadata md{};
    md.priority       = SerardPriorityNominal;
    md.transfer_kind  = SerardTransferKindMessage;
    md.port_id        = 123;
    md.remote_node_id = SERARD_NODE_ID_UNSET;   // message → remote must be unset
    md.transfer_id    = 0;

    TxBuffer txbuf;
    for (SerardNodeID node_id = 10; node_id < 20; node_id++)
    {
        // Source node_id (non-anonymous) so RX should create a session for node_id.
        REQUIRE(serardTxPush(node_id,
                            &md,
                            sizeof(dummy_payload),
                            dummy_payload,
                            &txbuf,
                            tx_emitter) > 0);

        SerardReassembler reas = serardReassemblerInit();

        constexpr int N = 50;
        for (int i = 0; i < N; i++)
        {
            size_t remaining    = txbuf.data.size();
            const uint8_t* ptr  = txbuf.data.data();

            while (remaining > 0)
            {
                size_t chunk = remaining;
                SerardRxTransfer out{};
                SerardRxSubscription* out_sub = nullptr;

                int8_t res = serardRxAccept(&rx,
                                            &reas,
                                            /*timestamp*/ 1000 + i,
                                            &chunk,
                                            ptr,
                                            /*redundant_transport_index*/ 0,
                                            &out,
                                            &out_sub);

                if (out.payload)
                {
                    rx.memory_payload.deallocate(rx.memory_payload.user_reference,
                                                out.payload_extent,
                                                out.payload);
                }

                ptr       += chunk;
                remaining -= chunk;

                if (res != 0)
                    break;
            }
        }
    }
    // Expected correct behavior: one session allocated and kept.
    CHECK(g_session_allocs == 1);
    CHECK(g_session_frees == 0);
}
