#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include <doctest/doctest.h>
#include "cyphal.hpp"

uint32_t make_service(CyphalPortID service_id, CyphalNodeID source_id, CyphalNodeID destination_id, bool response = false)
{
    return static_cast<uint32_t>(0x02000000) | 
        static_cast<uint32_t>((response & 0x1) << 23) | 
        static_cast<uint32_t>((service_id & 0x1FF) << 14U) | 
        static_cast<uint32_t>((destination_id & 0x7F) << 7U) | 
        static_cast<uint32_t>(source_id & 0x7F);
}

uint32_t make_message(CyphalPortID subject_id, CyphalNodeID source_id)
{
    return static_cast<uint32_t>((subject_id & 0x1FFF) << 8U) | 
        static_cast<uint32_t>(source_id & 0x7F);
}

TEST_CASE("is_service")
{
    CHECK_FALSE(is_service(0));
    CHECK(is_service(0x02000000));

    CHECK_FALSE(is_service(0x10340000));
    CHECK(      is_service(0x12348000));
}

TEST_CASE("parse_message_header basic")
{
    constexpr CyphalPortID port_id = 1234;
    constexpr CyphalNodeID source_id = 42;

    const uint32_t header = make_message(port_id, source_id);
    const CyphalHeader result = parse_message_header(header);

    CHECK_FALSE(result.is_service);
    CHECK(result.port_id == port_id);
    CHECK(result.source_id == source_id);
    CHECK(result.destination_id == CYPHAL_NODE_ID_UNSET);
}

TEST_CASE("parse_message_header maximum values")
{
    constexpr CyphalPortID port_id = 8191;     // 13 bits
    constexpr CyphalNodeID source_id = 127;    // 7 bits

    const uint32_t header = make_message(port_id, source_id);
    const CyphalHeader result = parse_message_header(header);

    CHECK_FALSE(result.is_service);
    CHECK(result.port_id == 8191);
    CHECK(result.source_id == 127);
    CHECK(result.destination_id == CYPHAL_NODE_ID_UNSET);
}

TEST_CASE("parse_service_header basic")
{
    constexpr CyphalPortID port_id = 321;
    constexpr CyphalNodeID destination_id = 55;
    constexpr CyphalNodeID source_id = 17;

    const uint32_t header = make_service(port_id, source_id, destination_id);

    const CyphalHeader result = parse_service_header(header);

    CHECK(result.is_service);
    CHECK(result.port_id == port_id);
    CHECK(result.destination_id == destination_id);
    CHECK(result.source_id == source_id);
}

TEST_CASE("parse_service_header maximum values")
{
    constexpr CyphalPortID port_id = 511;      // 9 bits
    constexpr CyphalNodeID destination_id = 127;
    constexpr CyphalNodeID source_id = 127;

    const uint32_t header = make_service(port_id, source_id, destination_id);

    const CyphalHeader result = parse_service_header(header);

    CHECK(result.is_service);
    CHECK(result.port_id == 511);
    CHECK(result.destination_id == 127);
    CHECK(result.source_id == 127);
}

TEST_CASE("parse_header dispatches message")
{
    constexpr CyphalPortID port_id = 100;
    constexpr CyphalNodeID source_id = 33;

    const uint32_t header = make_message(port_id, source_id);

    const CyphalHeader result = parse_header(header);

    CHECK_FALSE(result.is_service);
    CHECK(result.port_id == port_id);
    CHECK(result.source_id == source_id);
    CHECK(result.destination_id == CYPHAL_NODE_ID_UNSET);
}

TEST_CASE("parse_header dispatches service")
{
    constexpr CyphalPortID port_id = 77;
    constexpr CyphalNodeID destination_id = 88;
    constexpr CyphalNodeID source_id = 99;

    const uint32_t header = make_service(port_id, source_id, destination_id);

    const CyphalHeader result = parse_header(header);

    CHECK(result.is_service);
    CHECK(result.port_id == port_id);
    CHECK(result.destination_id == destination_id);
    CHECK(result.source_id == source_id);
}

TEST_CASE("parse_header heartbeat example 0x107D5579")
{
    constexpr uint32_t header = 0x107D5579;

    const CyphalHeader h = parse_header(header);

    CHECK_FALSE(h.is_service);
    CHECK(h.port_id == 7509);
    CHECK(h.source_id == 121);
    CHECK(h.destination_id == CYPHAL_NODE_ID_UNSET);
}

TEST_CASE("parse_header info request example 136B957B")
{
    constexpr uint32_t header = 0x136B957B;

    const CyphalHeader h = parse_header(header);

    CHECK(h.is_service);
    CHECK(h.port_id == 430);
    CHECK(h.source_id == 123);
    CHECK(h.destination_id == 42);
}

TEST_CASE("parse_header extracts all fields from known service header")
{
    const uint32_t header =
        0x02000000 |     // service
        (123U << 14U) |
        (42U  << 7U)  |
        17U;

    const CyphalHeader result = parse_header(header);

    CHECK(result.is_service);
    CHECK(result.port_id == 123);
    CHECK(result.destination_id == 42);
    CHECK(result.source_id == 17);
}