#pragma once

#include "SocketConcept.hpp"

extern void print(const char* buffer);


struct Endpoint
{
    IPv4Address address;
    uint16_t    port;
};

template<typename Driver, typename Driver::SOCKET_NUMBER socket_number>
class SocketBase
{
protected:
    SocketBase(Driver& driver) : driver_(driver) {}

public:
    uint16_t available() const
    {
        return driver_.rxAvailable(socket_number);
    }

    bool poll() const
    {
        return available() > 0U;
    }

    uint8_t status() const
    {
        return this->driver_.socketStatus(socket_number);
    }


protected:
    Driver& driver_;
};

template<typename Driver, typename Driver::SOCKET_NUMBER socket_number, uint16_t local_port>
class UdpSocket : public SocketBase<Driver, socket_number>
{
public:
    UdpSocket(Driver& driver) : SocketBase<Driver, socket_number>(driver) {}

    bool open()
    {
        return this->driver_.openUdp(socket_number, local_port);
    }

    void close()
    {
        this->driver_.closeUdp(socket_number);
    }

    bool reopen()
    {
        this->driver_.closeUdp(socket_number);
        return this->driver_.openUdp(socket_number, local_port);
    }

    bool send(const uint8_t* data, uint16_t size, const Endpoint& destination)
    {
        return this->driver_.sendUdp(socket_number, data, size, destination.address, destination.port) >= 0;
    }

    int32_t receive(uint8_t* data, uint16_t size, Endpoint& source)
    {
        return this->driver_.receiveUdp(socket_number, data, size, source.address, source.port);
    }
};

template<TcpSocketConcept  Driver, typename Driver::SOCKET_NUMBER socket_number, uint16_t local_port>
class TcpSocket : public SocketBase<Driver, socket_number>
{
public:
    TcpSocket(Driver& driver) : SocketBase<Driver, socket_number>(driver) {}

    bool open()
    {
        return this->driver_.openTcp(socket_number, local_port);
    }

    void close()
    {
        this->driver_.closeTcp(socket_number);
    }

    bool reopen()
    {
        this->driver_.closeUdp(socket_number);
        return this->driver_.openUdp(socket_number, local_port);
    }

    bool connectTo(const Endpoint& destination)
    {
        return this->driver_.connectTcp(socket_number, destination.address, destination.port);
    }

    bool disconnect()
    {
        return this->driver_.disconnectTcp(socket_number);
    }

    bool listen()
    {
        return this->driver_.listenTcp(socket_number);
    }

    uint16_t rxAvailable() const
    {
        return this->driver_.rxAvailable(socket_number);
    }

    uint16_t txAvailable() const
    {
        return this->driver_.txAvailable(socket_number);
    }

    int32_t send(
        const uint8_t* data, uint16_t size)
    {
        return this->driver_.sendTcp(socket_number, data, size);
    }

    int32_t receive(uint8_t* data, uint16_t size)
    {
        return this->driver_.receiveTcp(socket_number, data, size);
    }
};
