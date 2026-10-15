#pragma once

#include "W5500Driver.hpp"

struct Endpoint
{
    IPv4Address address;
    uint16_t    port;
};

template<typename Driver>
class SocketBase
{
protected:
    SocketBase(Driver& driver, uint8_t socket_number) : driver_(driver), socket_(socket_number) {}

protected:
    Driver& driver_;
    uint8_t socket_;
};

template<W5500DriverUdpConcept Driver>
class UdpSocket : public SocketBase<Driver>
{
public:
    UdpSocket(Driver& driver, uint8_t socket_number) : SocketBase<Driver>(driver, socket_number) {}

    bool open(uint16_t local_port)
    {
        return this->driver_.openUdp(this->socket_, local_port);
    }

    void close()
    {
        this->driver_.closeUdp(this->socket_);
    }

    bool send(const uint8_t* data, uint16_t size, const Endpoint& destination)
    {
        return this->driver_.sendUdp(this->socket_, data, size, destination.address, destination.port) >= 0;
    }

    int32_t receive(uint8_t* data, uint16_t size, Endpoint& source)
    {
        return this->driver_.receiveUdp(this->socket_, data, size, source.address, source.port);
    }
};

template<W5500DriverTcpConcept  Driver>
class TcpSocket : public SocketBase<Driver>
{
public:
    TcpSocket(Driver& driver, uint8_t socket_number) : SocketBase<Driver>(driver, socket_number) {}

    bool open(uint16_t local_port)
    {
        return this->driver_.openTcp(this->socket_, local_port);
    }

    void close()
    {
        this->driver_.closeTcp(this->socket_);
    }

    bool connectTo(const IPv4Address& remote_address, uint16_t remote_port)
    {
        return this->driver_.connectTcp(this->socket_, remote_address, remote_port);
    }

    bool disconnect()
    {
        return this->driver_.disconnectTcp(this->socket_);
    }

    bool listen()
    {
        return this->driver_.listenTcp(this->socket_);
    }

    uint8_t status() const
    {
        return this->driver_.socketStatus(this->socket_);
    }

    uint16_t rxAvailable() const
    {
        return this->driver_.rxAvailable(this->socket_);
    }

    uint16_t txAvailable() const
    {
        return this->driver_.txAvailable(this->socket_);
    }

    int32_t send(
        const uint8_t* data, uint16_t size)
    {
        return this->driver_.sendTcp(this->socket_, data, size);
    }

    int32_t receive(uint8_t* data, uint16_t size)
    {
        return this->driver_.receiveTcp(this->socket_, data, size);
    }
};