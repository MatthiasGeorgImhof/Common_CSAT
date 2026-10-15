#pragma once

#include <cstdint>
#include <cstring>

#include "Transport.hpp"
#include "W5500Concept.hpp"

extern "C"
{
#include "wizchip_conf.h"
#include "socket.h"
}

template <typename Transport>
class W5500Driver
{
public:
    explicit W5500Driver(Transport &transport) : transport_(transport) {}

    bool initialize()
    {
        registerCallbacks();

        uint8_t tx_mem[8] = {2, 2, 2, 2, 2, 2, 2, 2};
        uint8_t rx_mem[8] = {2, 2, 2, 2, 2, 2, 2, 2};

        return wizchip_init(tx_mem, rx_mem) == 0;
    }

    bool setNetworkInfo(const wiz_NetInfo &info)
    {
        wiz_NetInfo copy = info;
        wizchip_setnetinfo(&copy);
        return true;
    }

    wiz_NetInfo getNetworkInfo() const
    {
        wiz_NetInfo info{};
        wizchip_getnetinfo(&info);
        return info;
    }

    bool linkUp() const
    {
        return wizphy_getphylink() == PHY_LINK_ON;
    }

    bool openUdp(uint8_t socket_number, uint16_t local_port)
    {
        closeUdp(socket_number);

        return socket(socket_number, Sn_MR_UDP, local_port, 0) == socket_number;
    }

    void closeUdp(uint8_t socket_number)
    {
        closesocket(socket_number);
    }

    bool openTcp(uint8_t socket_number, uint16_t local_port)
    {
        closeTcp(socket_number);

        return socket(socket_number, Sn_MR_TCP, local_port, 0) == socket_number;
    }

    void closeTcp(uint8_t socket_number)
    {
        closesocket(socket_number);
    }

    bool connectTcp(
        uint8_t socket_number,
        const IPv4Address& remote_address,
        uint16_t remote_port)
    {
        return ::connect(socket_number, const_cast<uint8_t*>(remote_address.octet), remote_port) == SOCK_OK;
    }

    bool disconnectTcp(uint8_t socket_number)
    {
        return ::disconnect(socket_number) == SOCK_OK;
    }

    bool listenTcp(uint8_t socket_number)
    {
        return ::listen(socket_number) == SOCK_OK;
    }

    int32_t sendTcp(uint8_t socket_number, const uint8_t* data, uint16_t size)
    {
        return send(socket_number, const_cast<uint8_t*>(data), size);
    }

    int32_t receiveTcp(uint8_t socket_number, uint8_t* data, uint16_t size)
    {
        return recv(socket_number, data,size);
    }

    uint8_t socketStatus(uint8_t socket_number) const
    {
        return getSn_SR(socket_number);
    }

    uint16_t rxAvailable(uint8_t socket_number) const
    {
        return getSn_RX_RSR(socket_number);
    }

    uint16_t txAvailable(uint8_t socket_number) const
    {
        return getSn_TX_FSR(socket_number);
    }

    int32_t sendUdp(uint8_t socket_number, const uint8_t *data, uint16_t size, const IPv4Address &destination, uint16_t destination_port)
    {
        return sendto(socket_number, const_cast<uint8_t *>(data), size, const_cast<uint8_t *>(destination.octet), destination_port);
    }

    int32_t receiveUdp(uint8_t socket_number, uint8_t *data, uint16_t size, IPv4Address &source, uint16_t &source_port)
    {
        return recvfrom(socket_number, data, size, source.octet, &source_port);
    }

public:
    void wizchipSelect()
    {
        transport_.select();
    }

    void wizchipDeselect()
    {
        transport_.deselect();
    }

    uint8_t wizchipReadByte()
    {
        uint8_t rx = 0;
        transport_.read_nocs(&rx, 1);
        return rx;
    }

    void wizchipWriteByte(uint8_t data)
    {
        transport_.write_nocs(&data, 1);
    }

    void wizchipReadBurst(uint8_t *buffer, uint16_t length)
    {
        transport_.read_nocs(buffer, length);
    }

    void wizchipWriteBurst(uint8_t *buffer, uint16_t length)
    {
        transport_.write_nocs(buffer, length);
    }

private:
    void registerCallbacks()
    {
        reg_wizchip_cs_cbfunc(&wizchipSelect, &wizchipDeselect);

        reg_wizchip_spi_cbfunc(&wizchipReadByte, &wizchipWriteByte);

        reg_wizchip_spiburst_cbfunc(&wizchipReadBurst, &wizchipWriteBurst);
    }

private:
    Transport &transport_;
};

// static_assert(W5500DriverUdpConcept<W5500Driver<MyTransport>>);

// static_assert(W5500DriverTcpConcept<W5500Driver<MyTransport>>);