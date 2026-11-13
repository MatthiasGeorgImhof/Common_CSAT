#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "Mram.hpp"
#include "MramRegister.hpp"
#include "MramStorageBackend.hpp"
#include "MramStore.hpp"
#include "RamBufferBackend.hpp"

// ------------------------------------------------------------
// Minimal mock mirroring uavcan.register.Value.1.0
// ------------------------------------------------------------
struct MockRegisterValue
{
    enum class Tag { Empty, Natural32, Real32, Unstructured };

    Tag                        tag = Tag::Empty;
    std::vector<std::uint32_t> natural32;
    std::vector<float>         real32;
    std::vector<std::uint8_t>  unstructured;
};

// ------------------------------------------------------------
// Test payloads and layout
// ------------------------------------------------------------
struct RegConfig
{
    std::uint32_t baudrate;
    std::uint32_t node_id;
};

struct RegGain
{
    float kp;
};

struct RegBlob
{
    std::uint8_t bytes[6];
};

struct RegLayout
{
    MramRecord<RegConfig> config;
    MramRecord<RegGain>   gain;
    MramRecord<RegBlob>   blob;
};

template <>
struct MramLayoutMembers<RegLayout>
{
    static constexpr auto members = std::make_tuple(
        &RegLayout::config,
        &RegLayout::gain,
        &RegLayout::blob
    );
};

template <>
struct MramTraits<RegConfig, RegLayout>
{
    static constexpr std::uint32_t offset  = offsetof(RegLayout, config);
    static constexpr std::uint32_t version = 1;
    static constexpr std::size_t   index   = 0;
    static constexpr const char*   name    = "can.config";
};

template <>
struct MramTraits<RegGain, RegLayout>
{
    static constexpr std::uint32_t offset  = offsetof(RegLayout, gain);
    static constexpr std::uint32_t version = 1;
    static constexpr std::size_t   index   = 1;
    static constexpr const char*   name    = "ctrl.kp";
};

template <>
struct MramTraits<RegBlob, RegLayout>
{
    static constexpr std::uint32_t offset  = offsetof(RegLayout, blob);
    static constexpr std::uint32_t version = 1;
    static constexpr std::size_t   index   = 2;
    static constexpr const char*   name    = "device.uid";
};

// ------------------------------------------------------------
// Generic unstructured fallback codec + typed specializations
// ------------------------------------------------------------
template <typename T>
struct MramRegisterCodec<T, MockRegisterValue>
{
    static bool encode(const T& payload, MockRegisterValue& out)
    {
        out.tag = MockRegisterValue::Tag::Unstructured;
        out.unstructured.resize(sizeof(T));
        std::memcpy(out.unstructured.data(), &payload, sizeof(T));
        return true;
    }

    static bool decode(const MockRegisterValue& in, T& payload)
    {
        if (in.tag != MockRegisterValue::Tag::Unstructured ||
            in.unstructured.size() != sizeof(T))
            return false;

        std::memcpy(&payload, in.unstructured.data(), sizeof(T));
        return true;
    }
};

template <>
struct MramRegisterCodec<RegConfig, MockRegisterValue>
{
    static bool encode(const RegConfig& payload, MockRegisterValue& out)
    {
        out.tag = MockRegisterValue::Tag::Natural32;
        out.natural32 = {payload.baudrate, payload.node_id};
        return true;
    }

    static bool decode(const MockRegisterValue& in, RegConfig& payload)
    {
        if (in.tag != MockRegisterValue::Tag::Natural32 ||
            in.natural32.size() != 2)
            return false;
        if (in.natural32[1] > 127) // validate node_id range
            return false;

        payload.baudrate = in.natural32[0];
        payload.node_id  = in.natural32[1];
        return true;
    }
};

template <>
struct MramRegisterCodec<RegGain, MockRegisterValue>
{
    static bool encode(const RegGain& payload, MockRegisterValue& out)
    {
        out.tag = MockRegisterValue::Tag::Real32;
        out.real32 = {payload.kp};
        return true;
    }

    static bool decode(const MockRegisterValue& in, RegGain& payload)
    {
        if (in.tag != MockRegisterValue::Tag::Real32 ||
            in.real32.size() != 1)
            return false;

        payload.kp = in.real32[0];
        return true;
    }
};

// Compile-time checks
using RegTable = MramRegisterTable<RegLayout, MockRegisterValue>;

static_assert(MramRegisterCodecConcept<RegConfig, MockRegisterValue>);
static_assert(MramRegisterCodecConcept<RegGain, MockRegisterValue>);
static_assert(MramRegisterCodecConcept<RegBlob, MockRegisterValue>);
static_assert(RegTable::entries.size() == 3);
static_assert(RegTable::lookup("ctrl.kp") != nullptr);
static_assert(RegTable::lookup("ctrl.kp")->mram->index == 1);
static_assert(RegTable::lookup_by_index(2) != nullptr);
static_assert(RegTable::lookup_by_index(99) == nullptr);

// ------------------------------------------------------------
// Tests
// ------------------------------------------------------------
TEST_CASE("MramRegisterTable: lookup and list by index")
{
    std::vector<std::string> names;
    for (std::size_t idx = 0;; ++idx)
    {
        const auto* entry = RegTable::lookup_by_index(idx);
        if (!entry)
            break;
        names.emplace_back(entry->mram->name);
    }

    REQUIRE(names.size() == 3);
    CHECK(names[0] == "can.config");
    CHECK(names[1] == "ctrl.kp");
    CHECK(names[2] == "device.uid");

    CHECK(RegTable::lookup("missing") == nullptr);
    CHECK(RegTable::lookup("") == nullptr);
}

TEST_CASE("MramRegisterTable: typed encode_raw and decode_raw round-trip")
{
    const auto* cfg_reg = RegTable::lookup("can.config");
    REQUIRE(cfg_reg != nullptr);

    const RegConfig in_cfg{1000000, 42};
    MockRegisterValue val{};
    REQUIRE(cfg_reg->encode_raw(&in_cfg, val) == true);
    CHECK(val.tag == MockRegisterValue::Tag::Natural32);
    REQUIRE(val.natural32.size() == 2);
    CHECK(val.natural32[0] == 1000000);
    CHECK(val.natural32[1] == 42);

    RegConfig out_cfg{};
    REQUIRE(cfg_reg->decode_raw(val, &out_cfg) == true);
    CHECK(out_cfg.baudrate == 1000000);
    CHECK(out_cfg.node_id  == 42);

    const auto* gain_reg = RegTable::lookup("ctrl.kp");
    REQUIRE(gain_reg != nullptr);

    const RegGain in_gain{2.5f};
    REQUIRE(gain_reg->encode_raw(&in_gain, val) == true);
    CHECK(val.tag == MockRegisterValue::Tag::Real32);

    RegGain out_gain{};
    REQUIRE(gain_reg->decode_raw(val, &out_gain) == true);
    CHECK(out_gain.kp == doctest::Approx(2.5f));
}

TEST_CASE("MramRegisterTable: unstructured fallback codec round-trip")
{
    const auto* blob_reg = RegTable::lookup("device.uid");
    REQUIRE(blob_reg != nullptr);

    const RegBlob in_blob{{0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02}};
    MockRegisterValue val{};
    REQUIRE(blob_reg->encode_raw(&in_blob, val) == true);
    CHECK(val.tag == MockRegisterValue::Tag::Unstructured);
    REQUIRE(val.unstructured.size() == sizeof(RegBlob));

    RegBlob out_blob{};
    REQUIRE(blob_reg->decode_raw(val, &out_blob) == true);
    CHECK(std::memcmp(in_blob.bytes, out_blob.bytes, sizeof(RegBlob)) == 0);
}

TEST_CASE("MramRegisterTable: decode_raw rejects invalid types, sizes, values, and nullptrs")
{
    const auto* cfg_reg = RegTable::lookup("can.config");
    REQUIRE(cfg_reg != nullptr);

    RegConfig target{500000, 10};

    SUBCASE("Wrong tag")
    {
        MockRegisterValue wrong_tag{};
        wrong_tag.tag = MockRegisterValue::Tag::Real32;
        wrong_tag.real32 = {1.0f};
        CHECK(cfg_reg->decode_raw(wrong_tag, &target) == false);
    }

    SUBCASE("Wrong array length")
    {
        MockRegisterValue wrong_len{};
        wrong_len.tag = MockRegisterValue::Tag::Natural32;
        wrong_len.natural32 = {500000};
        CHECK(cfg_reg->decode_raw(wrong_len, &target) == false);
    }

    SUBCASE("Semantic validation failure (node_id > 127)")
    {
        MockRegisterValue bad_val{};
        bad_val.tag = MockRegisterValue::Tag::Natural32;
        bad_val.natural32 = {500000, 200};
        CHECK(cfg_reg->decode_raw(bad_val, &target) == false);
    }

    SUBCASE("Null pointers")
    {
        MockRegisterValue val{};
        CHECK(cfg_reg->encode_raw(nullptr, val) == false);
        CHECK(cfg_reg->decode_raw(val, nullptr) == false);
    }

    // Target remained untouched on all failures
    CHECK(target.baudrate == 500000);
    CHECK(target.node_id  == 10);
}

TEST_CASE("MramRegisterTable: simulated OpenCyphal Access (Get/Set) with RamBufferBackend")
{
    std::array<std::uint8_t, sizeof(RegLayout)> mram_buf{};
    mram_buf.fill(0xFF);
    RamBufferBackend backend(mram_buf.data(), mram_buf.size());
    MramStore<RegLayout, RamBufferBackend> store(backend);

    // Seed initial values via C++ API
    REQUIRE(store.set(RegConfig{250000, 5}) == true);

    auto access = [&](std::string_view name, const MockRegisterValue& req) -> MockRegisterValue {
        const auto* reg = RegTable::lookup(name);
        if (!reg)
            return MockRegisterValue{}; // Empty

        alignas(std::max_align_t) std::uint8_t scratch[sizeof(RegLayout)]{};
        if (req.tag != MockRegisterValue::Tag::Empty)
        {
            if (reg->decode_raw(req, scratch))
                backend.writeRecord(*reg->mram, reg->mram->payload_offset,
                                    scratch, reg->mram->payload_size);
        }

        MockRegisterValue resp{};
        if (backend.readRecord(*reg->mram, reg->mram->payload_offset,
                               scratch, reg->mram->payload_size))
        {
            reg->encode_raw(scratch, resp);
        }
        return resp;
    };

    // GET existing register
    MockRegisterValue empty_req{};
    auto get_resp = access("can.config", empty_req);
    CHECK(get_resp.tag == MockRegisterValue::Tag::Natural32);
    CHECK(get_resp.natural32 == std::vector<std::uint32_t>{250000, 5});

    // SET register
    MockRegisterValue set_req{};
    set_req.tag = MockRegisterValue::Tag::Natural32;
    set_req.natural32 = {1000000, 12};
    auto set_resp = access("can.config", set_req);
    CHECK(set_resp.natural32 == std::vector<std::uint32_t>{1000000, 12});

    // GET unknown register returns Empty
    CHECK(access("does.not.exist", empty_req).tag == MockRegisterValue::Tag::Empty);
}