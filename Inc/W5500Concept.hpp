#pragma once

#include <concepts>
#include <cstdint>

struct IPv4Address
{
    uint8_t octet[4];
};

struct NetworkInfo
{
    uint8_t mac[6];

    IPv4Address ip;
    IPv4Address subnet;
    IPv4Address gateway;
    IPv4Address dns;

    bool dhcp = false;
};



template <typename Driver>
concept W5500DriverSocketConcept =
    requires(
        Driver driver,
        uint8_t socket_number) {
        { driver.socketStatus(socket_number) } -> std::convertible_to<uint8_t>;
        { driver.rxAvailable(socket_number) } -> std::convertible_to<uint16_t>;
        { driver.txAvailable(socket_number) } -> std::convertible_to<uint16_t>;
    };

template <typename Driver>
concept W5500DriverUdpConcept = W5500DriverSocketConcept<Driver> &&
    requires(
        Driver driver,
        uint8_t socket_number,
        uint16_t port,
        const uint8_t *tx_data,
        uint8_t *rx_data,
        uint16_t size,
        IPv4Address address,
        uint16_t remote_port) {
        { driver.openUdp(socket_number, port) } -> std::same_as<bool>;

        { driver.closeUdp(socket_number) } -> std::same_as<void>;

        {
            driver.sendUdp(
                socket_number,
                tx_data,
                size,
                address,
                remote_port)
        } -> std::convertible_to<int32_t>;

        {
            driver.receiveUdp(
                socket_number,
                rx_data,
                size,
                address,
                remote_port)
        } -> std::convertible_to<int32_t>;
    };

template <typename Driver>
concept W5500DriverTcpConcept = W5500DriverSocketConcept<Driver> &&
    requires(
        Driver driver,
        uint8_t socket_number,
        uint16_t port,
        const uint8_t *tx_data,
        uint8_t *rx_data,
        uint16_t size,
        IPv4Address address,
        uint16_t remote_port) {
        { driver.openTcp(socket_number, port) } -> std::same_as<bool>;

        { driver.closeTcp(socket_number) } -> std::same_as<void>;

        {
            driver.connectTcp(
                socket_number,
                address,
                remote_port)
        } -> std::same_as<bool>;

        {
            driver.disconnectTcp(
                socket_number)
        } -> std::same_as<bool>;

        {
            driver.listenTcp(
                socket_number)
        } -> std::same_as<bool>;

        {
            driver.sendTcp(
                socket_number,
                tx_data,
                size)
        } -> std::convertible_to<int32_t>;

        {
            driver.receiveTcp(
                socket_number,
                rx_data,
                size)
        } -> std::convertible_to<int32_t>;
    };