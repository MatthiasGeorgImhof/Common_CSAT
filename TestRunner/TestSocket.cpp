#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include "doctest/doctest.h"
#include "Socket.hpp"

class MockDriver
{
public:
    enum SOCKET_NUMBER : uint8_t {
        SOCKET_0 = 0,
        SOCKET_1 = 1,
        SOCKET_2 = 2,
        SOCKET_3 = 3,
        SOCKET_4 = 4,
        SOCKET_5 = 5,
        SOCKET_6 = 6,
        SOCKET_7 = 7,
        DNS_SOCKET = 6,
        DHCP_SOCKET = 7,
    };

public:
    // --- UDP Tracking ---
    bool open_called = false;
    bool close_called = false;
    uint8_t open_socket = 0;
    uint16_t open_port = 0;
    uint8_t close_socket = 0;

    int32_t send_result = 0;
    int32_t receive_result = 0;
    uint8_t last_send_socket = 0;
    uint16_t last_send_size = 0;

    bool openUdp(uint8_t socket, uint16_t port)
    {
        open_called = true;
        open_socket = socket;
        open_port = port;
        return true;
    }

    void closeUdp(uint8_t socket)
    {
        close_called = true;
        close_socket = socket;
    }

    int32_t sendUdp(uint8_t socket, const uint8_t *, uint16_t size, const IPv4Address &, uint16_t)
    {
        last_send_socket = socket;
        last_send_size = size;
        return send_result;
    }

    int32_t receiveUdp(uint8_t, uint8_t *, uint16_t, IPv4Address &, uint16_t &)
    {
        return receive_result;
    }

    // --- TCP Tracking ---
    bool open_tcp_called = false;
    bool close_tcp_called = false;
    bool connect_tcp_called = false;
    bool disconnect_tcp_called = false;
    bool listen_tcp_called = false;

    uint8_t tcp_socket = 0;
    uint16_t tcp_port = 0;
    IPv4Address tcp_destination_ip{};
    uint16_t tcp_destination_port = 0;

    bool open_tcp_result = true;
    bool connect_tcp_result = true;
    bool disconnect_tcp_result = true;
    bool listen_tcp_result = true;
    int32_t send_tcp_result = 0;
    int32_t receive_tcp_result = 0;

    uint8_t mock_status = 0;
    uint16_t mock_rx_available = 0;
    uint16_t mock_tx_available = 0;

    bool openTcp(uint8_t socket, uint16_t port)
    {
        open_tcp_called = true;
        tcp_socket = socket;
        tcp_port = port;
        return open_tcp_result;
    }

    void closeTcp(uint8_t socket)
    {
        close_tcp_called = true;
        tcp_socket = socket;
    }

    bool connectTcp(uint8_t socket, const IPv4Address& address, uint16_t port)
    {
        connect_tcp_called = true;
        tcp_socket = socket;
        tcp_destination_ip = address;
        tcp_destination_port = port;
        return connect_tcp_result;
    }

    bool disconnectTcp(uint8_t socket)
    {
        disconnect_tcp_called = true;
        tcp_socket = socket;
        return disconnect_tcp_result;
    }

    bool listenTcp(uint8_t socket)
    {
        listen_tcp_called = true;
        tcp_socket = socket;
        return listen_tcp_result;
    }

    int32_t sendTcp(uint8_t socket, const uint8_t*, uint16_t size)
    {
        last_send_socket = socket;
        last_send_size = size;
        return send_tcp_result;
    }

    int32_t receiveTcp(uint8_t socket, uint8_t*, uint16_t)
    {
        tcp_socket = socket;
        return receive_tcp_result;
    }

    // --- Shared Status & Buffer Concept Methods ---
    uint8_t socketStatus(uint8_t) const
    {
        return mock_status;
    }

    uint16_t rxAvailable(uint8_t) const
    {
        return mock_rx_available;
    }

    uint16_t txAvailable(uint8_t) const
    {
        return mock_tx_available;
    }
};

// ================= UDP Tests =================

TEST_CASE("UdpSocket open forwards to driver")
{
    constexpr static uint16_t LOCAL_PORT = 9382;
    
    MockDriver driver;
    UdpSocket<MockDriver, MockDriver::SOCKET_3, LOCAL_PORT> socket(driver);

    CHECK(socket.open());
    CHECK(driver.open_called);
    CHECK(driver.open_socket == MockDriver::SOCKET_3);
    CHECK(driver.open_port == LOCAL_PORT);
}

TEST_CASE("UdpSocket close forwards to driver")
{
    constexpr static uint16_t LOCAL_PORT = 9383;

    MockDriver driver;
    UdpSocket<MockDriver, MockDriver::SOCKET_3, LOCAL_PORT> socket(driver);

    socket.close();
    CHECK(driver.close_called);
    CHECK(driver.close_socket == MockDriver::SOCKET_3);
}

TEST_CASE("UdpSocket send reports success")
{
    constexpr static uint16_t LOCAL_PORT = 9384;

    MockDriver driver;
    driver.send_result = 10;
    UdpSocket<MockDriver, MockDriver::SOCKET_3, LOCAL_PORT> socket(driver);

    Endpoint destination{};
    uint8_t data[10] = {};

    CHECK(socket.send(data, sizeof(data), destination));
    CHECK(driver.last_send_socket == MockDriver::SOCKET_3);
    CHECK(driver.last_send_size == 10);
}

TEST_CASE("UdpSocket send reports failure")
{
    constexpr static uint16_t LOCAL_PORT = 9385;

    MockDriver driver;
    driver.send_result = -1;
    UdpSocket<MockDriver, MockDriver::SOCKET_3, LOCAL_PORT> socket(driver);

    Endpoint destination{};
    uint8_t data[10] = {};

    CHECK_FALSE(socket.send(data, sizeof(data), destination));
}

TEST_CASE("UdpSocket receive forwards driver result")
{
    constexpr static uint16_t LOCAL_PORT = 9386;

    MockDriver driver;
    driver.receive_result = 42;
    UdpSocket<MockDriver, MockDriver::SOCKET_3, LOCAL_PORT> socket(driver);

    Endpoint source{};
    uint8_t buffer[100] = {};

    CHECK(socket.receive(buffer, sizeof(buffer), source) == 42);
}

// ================= TCP Tests =================

TEST_CASE("TcpSocket open forwards to driver")
{
    constexpr static uint16_t LOCAL_PORT = 8080;

    MockDriver driver;
    TcpSocket<MockDriver, MockDriver::SOCKET_2, LOCAL_PORT> socket(driver);

    CHECK(socket.open());
    CHECK(driver.open_tcp_called);
    CHECK(driver.tcp_socket == MockDriver::SOCKET_2);
    CHECK(driver.tcp_port == 8080);
}

TEST_CASE("TcpSocket close forwards to driver")
{
    constexpr static uint16_t LOCAL_PORT = 8081;

    MockDriver driver;
    TcpSocket<MockDriver, MockDriver::SOCKET_2, LOCAL_PORT> socket(driver);

    socket.close();
    CHECK(driver.close_tcp_called);
    CHECK(driver.tcp_socket == MockDriver::SOCKET_2);
}

TEST_CASE("TcpSocket connectTo forwards destination to driver")
{
    constexpr static uint16_t LOCAL_PORT = 8082;
    MockDriver driver;
    
    TcpSocket<MockDriver, MockDriver::SOCKET_2, LOCAL_PORT> socket(driver);

    Endpoint destination{IPv4Address{192, 168, 1, 100}, 443};

    CHECK(socket.connectTo(destination));
    CHECK(driver.connect_tcp_called);
    CHECK(driver.tcp_socket == MockDriver::SOCKET_2);
    CHECK(memcmp(driver.tcp_destination_ip.bytes, destination.address.bytes, sizeof(destination.address.bytes)) == 0);
    CHECK(driver.tcp_destination_port == 443);
}

TEST_CASE("TcpSocket disconnect forwards to driver")
{
    constexpr static uint16_t LOCAL_PORT = 8083;

    MockDriver driver;
    TcpSocket<MockDriver, MockDriver::SOCKET_2, LOCAL_PORT> socket(driver);

    CHECK(socket.disconnect());
    CHECK(driver.disconnect_tcp_called);
    CHECK(driver.tcp_socket == MockDriver::SOCKET_2);
}

TEST_CASE("TcpSocket listen forwards to driver")
{
    constexpr static uint16_t LOCAL_PORT = 8084;

    MockDriver driver;
    TcpSocket<MockDriver, MockDriver::SOCKET_2, LOCAL_PORT> socket(driver);

    CHECK(socket.listen());
    CHECK(driver.listen_tcp_called);
    CHECK(driver.tcp_socket == MockDriver::SOCKET_2);
}

TEST_CASE("TcpSocket status, rxAvailable, txAvailable forward to driver")
{
    constexpr static uint16_t LOCAL_PORT = 8085;

    MockDriver driver;
    driver.mock_status = 0x17; // e.g. SOCK_ESTABLISHED
    driver.mock_rx_available = 128;
    driver.mock_tx_available = 1024;

    TcpSocket<MockDriver, MockDriver::SOCKET_2, LOCAL_PORT> socket(driver);

    CHECK(socket.status() == 0x17);
    CHECK(socket.rxAvailable() == 128);
    CHECK(socket.txAvailable() == 1024);
    CHECK(socket.available() == 128);
    CHECK(socket.poll() == true);
}

TEST_CASE("TcpSocket send forwards buffer and size")
{
    constexpr static uint16_t LOCAL_PORT = 8086;

    MockDriver driver;
    driver.send_tcp_result = 5;
    TcpSocket<MockDriver, MockDriver::SOCKET_2, LOCAL_PORT> socket(driver);

    uint8_t data[5] = {1, 2, 3, 4, 5};

    CHECK(socket.send(data, sizeof(data)) == 5);
    CHECK(driver.last_send_socket == MockDriver::SOCKET_2);
    CHECK(driver.last_send_size == 5);
}

TEST_CASE("TcpSocket receive forwards buffer and size")
{
    constexpr static uint16_t LOCAL_PORT = 8087;

    MockDriver driver;
    driver.receive_tcp_result = 16;
    TcpSocket<MockDriver, MockDriver::SOCKET_2, LOCAL_PORT> socket(driver);

    uint8_t buffer[32] = {};

    CHECK(socket.receive(buffer, sizeof(buffer)) == 16);
    CHECK(driver.tcp_socket == MockDriver::SOCKET_2);
}