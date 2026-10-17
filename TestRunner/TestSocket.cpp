#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include "doctest/doctest.h"
#include "Socket.hpp"

class MockDriver
{
public:
	enum SOCKET_NUMBER : uint8_t  {
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
    bool open_called = false;
    bool close_called = false;

    uint8_t open_socket = 0;
    uint16_t open_port = 0;

    uint8_t close_socket = 0;
    
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

    int32_t send_result = 0;
    int32_t receive_result = 0;

    uint8_t last_send_socket = 0;
    uint16_t last_send_size = 0;

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

    // Required by W5500DriverSocketConcept:
    uint8_t socketStatus(uint8_t) const
    {
        return 0;
    }

    uint16_t rxAvailable(uint8_t) const
    {
        return 0;
    }

    uint16_t txAvailable(uint8_t) const
    {
        return 0;
    }
};

TEST_CASE("UdpSocket open forwards to driver")
{
    MockDriver driver;

    UdpSocket socket(driver, MockDriver::SOCKET_3);

    CHECK(socket.open(9382));

    CHECK(driver.open_called);
    CHECK(driver.open_socket == MockDriver::SOCKET_3);
    CHECK(driver.open_port == 9382);
}

TEST_CASE("UdpSocket close forwards to driver")
{
    MockDriver driver;

    UdpSocket<MockDriver> socket(driver, MockDriver::SOCKET_3);

    socket.close();

    CHECK(driver.close_called);
    CHECK(driver.close_socket == MockDriver::SOCKET_3);
}

TEST_CASE("UdpSocket send reports success")
{
    MockDriver driver;
    driver.send_result = 10;

    UdpSocket<MockDriver> socket(driver, MockDriver::SOCKET_3);

    Endpoint destination{};

    uint8_t data[10] = {};

    CHECK(socket.send(data, sizeof(data), destination));

    CHECK(driver.last_send_socket == MockDriver::SOCKET_3);
    CHECK(driver.last_send_size == 10);
}

TEST_CASE("UdpSocket send reports failure")
{
    MockDriver driver;
    driver.send_result = -1;

    UdpSocket<MockDriver> socket(driver, MockDriver::SOCKET_3);

    Endpoint destination{};

    uint8_t data[10] = {};

    CHECK_FALSE(socket.send(data, sizeof(data), destination));
}

TEST_CASE("UdpSocket receive forwards driver result")
{
    MockDriver driver;
    driver.receive_result = 42;

    UdpSocket<MockDriver> socket(driver, MockDriver::SOCKET_3);

    Endpoint source{};

    uint8_t buffer[100] = {};

    CHECK(socket.receive(buffer, sizeof(buffer), source) == 42);
}