// 1. Suppress vendor header warnings across this test translation unit
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wzero-as-null-pointer-constant"

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <vector>
#include <cstring>

#include "SocketConcept.hpp"
#include "W5500Driver.hpp"

// Hardware stubs
extern "C" __attribute__((weak)) void HAL_Delay(uint32_t) {}
extern "C" __attribute__((weak)) uint8_t CDC_Transmit_FS(uint8_t*, uint16_t) { return 0; }
void print(const char*) {}

// Mock state
struct WiznetMockState {
    int reset_pin_low_count = 0;
    int reset_pin_high_count = 0;
    uint8_t version = 0x04;
    uint8_t phylink = PHY_LINK_ON;
    wiz_NetInfo current_netinfo{};

    int last_socket_opened = -1;
    uint8_t last_protocol = 0;
    uint16_t last_port = 0;
    int last_socket_closed = -1;

    int last_socket_connected = -1;
    uint8_t last_connect_ip[4] = {};
    uint16_t last_connect_port = 0;
    int last_socket_listen = -1;
    int last_socket_disconnect = -1;

    std::vector<uint8_t> last_tx_data;
    uint8_t last_dest_ip[4] = {};
    uint16_t last_dest_port = 0;
    int32_t mock_send_return = 0;
    int32_t mock_recv_return = 0;

    uint8_t mock_sr = 0;
    uint16_t mock_rx_rsr = 0;
    uint16_t mock_tx_fsr = 0;

    void reset() { *this = WiznetMockState{}; }
};

static WiznetMockState g_mock;

// Mock C function implementations matching exact Wiznet declarations
extern "C" {

// Low-level register access used by getVERSIONR(), getSn_SR(), getSn_RX_RSR(), getSn_TX_FSR()
uint8_t WIZCHIP_READ(uint32_t AddrSel) {
    uint16_t reg_offset = static_cast<uint16_t>((AddrSel >> 8) & 0xFFFF);
    if (reg_offset == 0x0039) return g_mock.version; // VERSIONR register

    uint8_t reg_low = static_cast<uint8_t>(reg_offset & 0xFF);
    if (reg_low == 0x03) return g_mock.mock_sr;                                     // Sn_SR
    if (reg_low == 0x20) return static_cast<uint8_t>(g_mock.mock_tx_fsr >> 8);      // Sn_TX_FSR MSB
    if (reg_low == 0x21) return static_cast<uint8_t>(g_mock.mock_tx_fsr & 0xFF);    // Sn_TX_FSR LSB
    if (reg_low == 0x26) return static_cast<uint8_t>(g_mock.mock_rx_rsr >> 8);      // Sn_RX_RSR MSB
    if (reg_low == 0x27) return static_cast<uint8_t>(g_mock.mock_rx_rsr & 0xFF);    // Sn_RX_RSR LSB

    return 0;
}

void WIZCHIP_WRITE(uint32_t, uint8_t) {}
void WIZCHIP_READ_BUF(uint32_t, uint8_t*, uint16_t) {}
void WIZCHIP_WRITE_BUF(uint32_t, uint8_t*, uint16_t) {}

int8_t ctlwizchip(ctlwizchip_type cwtype, void* arg) {
    if (cwtype == CW_INIT_WIZCHIP) return 0;
    if (cwtype == CW_GET_PHYLINK) {
        *static_cast<uint8_t*>(arg) = g_mock.phylink;
        return 0;
    }
    return 0;
}

int8_t ctlnetwork(ctlnetwork_type cntype, void* arg) {
    if (cntype == CN_SET_NETINFO) {
        g_mock.current_netinfo = *static_cast<wiz_NetInfo*>(arg);
        return 0;
    }
    if (cntype == CN_GET_NETINFO) {
        *static_cast<wiz_NetInfo*>(arg) = g_mock.current_netinfo;
        return 0;
    }
    return 0;
}

int8_t wizchip_init(uint8_t*, uint8_t*) { return 0; }
void   wizchip_getnetinfo(wiz_NetInfo* pnetinfo) { *pnetinfo = g_mock.current_netinfo; }
int8_t wizphy_getphylink(void) { return static_cast<int8_t>(g_mock.phylink); }

void reg_wizchip_cs_cbfunc(void(*)(void), void(*)(void)) {}
void reg_wizchip_spi_cbfunc(uint8_t(*)(void), void(*)(uint8_t)) {}
void reg_wizchip_spiburst_cbfunc(void(*)(uint8_t*, uint16_t), void(*)(uint8_t*, uint16_t)) {}

int8_t socket(uint8_t sn, uint8_t protocol, uint16_t port, uint8_t) {
    g_mock.last_socket_opened = sn;
    g_mock.last_protocol = protocol;
    g_mock.last_port = port;
    return static_cast<int8_t>(sn);
}

int8_t closesocket(uint8_t sn) {
    g_mock.last_socket_closed = sn;
    return SOCK_OK;
}

int8_t connect(uint8_t sn, uint8_t* addr, uint16_t port) {
    g_mock.last_socket_connected = sn;
    std::memcpy(g_mock.last_connect_ip, addr, 4);
    g_mock.last_connect_port = port;
    return SOCK_OK;
}

int8_t disconnect(uint8_t sn) {
    g_mock.last_socket_disconnect = sn;
    return SOCK_OK;
}

int8_t listen(uint8_t sn) {
    g_mock.last_socket_listen = sn;
    return SOCK_OK;
}

int32_t send(uint8_t, uint8_t* buf, uint16_t len) {
    g_mock.last_tx_data.assign(buf, buf + len);
    return g_mock.mock_send_return > 0 ? g_mock.mock_send_return : len;
}

int32_t recv(uint8_t, uint8_t*, uint16_t) {
    return g_mock.mock_recv_return;
}

// Wiznet's sendto macro routes here
int32_t sendto_W5x00(uint8_t, uint8_t* buf, uint16_t len, uint8_t* addr, uint16_t port) {
    g_mock.last_tx_data.assign(buf, buf + len);
    std::memcpy(g_mock.last_dest_ip, addr, 4);
    g_mock.last_dest_port = port;
    return g_mock.mock_send_return > 0 ? g_mock.mock_send_return : len;
}

int32_t recvfrom(uint8_t, uint8_t*, uint16_t, uint8_t*, uint16_t*) {
    return g_mock.mock_recv_return;
}

void DNS_init(uint8_t, uint8_t*) {}
void DHCP_init(uint8_t, uint8_t*) {}
void reg_dhcp_cbfunc(void(*)(void), void(*)(void), void(*)(void)) {}
uint8_t DHCP_run(void) { return DHCP_IP_ASSIGN; }
void getIPfromDHCP(uint8_t* ip) { ip[0]=10; ip[1]=0; ip[2]=0; ip[3]=50; }
void getGWfromDHCP(uint8_t* gw) { gw[0]=10; gw[1]=0; gw[2]=0; gw[3]=1; }
void getSNfromDHCP(uint8_t* sn) { sn[0]=255; sn[1]=255; sn[2]=255; sn[3]=0; }
void getDNSfromDHCP(uint8_t* dns) { dns[0]=1; dns[1]=1; dns[2]=1; dns[3]=1; }

uint16_t getSn_RX_RSR(uint8_t) { return g_mock.mock_rx_rsr; }
uint16_t getSn_TX_FSR(uint8_t) { return g_mock.mock_tx_fsr; } 
} // extern "C"

// --- Mock Hardware Types ---
struct MockResetPin {
    void low() { g_mock.reset_pin_low_count++; }
    void high() { g_mock.reset_pin_high_count++; }
};

struct MockTransport {
    bool selected = false;
    std::vector<uint8_t> written_data;

    void select() { selected = true; }
    void deselect() { selected = false; }
    void read_nocs(uint8_t* buffer, uint16_t length) {
        std::memset(buffer, 0xA5, length);
    }
    void write_nocs(const uint8_t* buffer, uint16_t length) {
        written_data.insert(written_data.end(), buffer, buffer + length);
    }
};

static constexpr StaticAddress static_config{
    .mac = {{0x00, 0x08, 0xDC, 0x11, 0x22, 0x33}},
    .config = {
        .address = {192, 168, 1, 42},
        .subnet  = {255, 255, 255, 0},
        .gateway = {192, 168, 1, 1},
        .dns     = {8, 8, 8, 8}
    }
};

static constexpr DynamicAddress dynamic_config{
    .mac = {{0x00, 0x08, 0xDC, 0xAA, 0xBB, 0xCC}}
};

using StaticDriver  = W5500Driver<MockTransport, MockResetPin, static_config>;
using DynamicDriver = W5500Driver<MockTransport, MockResetPin, dynamic_config>;

// Goal 1: Concept & Full Method Instantiation Checks
static_assert(UdpSocketConcept<StaticDriver>);
static_assert(TcpSocketConcept<StaticDriver>);
static_assert(UdpSocketConcept<DynamicDriver>);
static_assert(TcpSocketConcept<DynamicDriver>);

template class W5500Driver<MockTransport, MockResetPin, static_config>;
template class W5500Driver<MockTransport, MockResetPin, dynamic_config>;

// Goal 2: Unit tests
TEST_CASE_FIXTURE(MockTransport, "W5500Driver converts static configuration correctly")
{
    g_mock.reset();
    StaticDriver driver(*this);

    auto net = driver.convertToWiz();

    CHECK(net.ip[0] == 192);
    CHECK(net.ip[3] == 42);
    CHECK(net.mac[5] == 0x33);
    CHECK(net.dhcp == NETINFO_STATIC);
}

TEST_CASE_FIXTURE(MockTransport, "W5500Driver initialization handles reset sequence and version check")
{
    g_mock.reset();
    StaticDriver driver(*this);

    int res = driver.initialize();

    CHECK(res == 0);
    CHECK(g_mock.reset_pin_low_count == 1);
    CHECK(g_mock.reset_pin_high_count == 1);
    CHECK(g_mock.current_netinfo.ip[3] == 42);
}

TEST_CASE_FIXTURE(MockTransport, "openUdp and sendUdp")
{
    g_mock.reset();
    StaticDriver driver(*this);

    CHECK(driver.openUdp(StaticDriver::SOCKET_3, 4000));
    CHECK(g_mock.last_socket_closed == StaticDriver::SOCKET_3);
    CHECK(g_mock.last_socket_opened == StaticDriver::SOCKET_3);

    uint8_t payload[] = {1, 2, 3};
    IPv4Address dest{192, 168, 1, 100};
    CHECK(driver.sendUdp(StaticDriver::SOCKET_3, payload, sizeof(payload), dest, 5000) == 3);
}

TEST_CASE_FIXTURE(MockTransport, "openTcp and connectTcp")
{
    g_mock.reset();
    StaticDriver driver(*this);

    CHECK(driver.openTcp(StaticDriver::SOCKET_2, 80));
    CHECK(g_mock.last_socket_closed == StaticDriver::SOCKET_2);
    CHECK(g_mock.last_socket_opened == StaticDriver::SOCKET_2);

    IPv4Address remote{10, 0, 0, 1};
    CHECK(driver.connectTcp(StaticDriver::SOCKET_2, remote, 8080));
    CHECK(g_mock.last_socket_connected == StaticDriver::SOCKET_2);
}

TEST_CASE_FIXTURE(MockTransport, "status and buffer checks")
{
    g_mock.reset();
    g_mock.mock_sr = 0x14;
    g_mock.mock_rx_rsr = 512;
    g_mock.mock_tx_fsr = 1024;

    StaticDriver driver(*this);

    CHECK(driver.socketStatus(StaticDriver::SOCKET_0) == 0x14);
    CHECK(driver.rxAvailable(StaticDriver::SOCKET_0) == 512);
    CHECK(driver.txAvailable(StaticDriver::SOCKET_0) == 1024);
}