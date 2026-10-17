/*
 * Network.hpp
 *
 *  Created on: Oct 17, 2026
 *      Author: mgi
 */

#ifndef NETWORK_HPP_
#define NETWORK_HPP_

struct IPv4Address
{
    uint8_t bytes[4];
};

struct IPv4Config
{
    IPv4Address address;
    IPv4Address subnet;
    IPv4Address gateway;
    IPv4Address dns;
};

struct MacAddress
{
    uint8_t bytes[6];
};

struct StaticAddress
{
	MacAddress mac;
	IPv4Config config;
};

struct DynamicAddress
{
	MacAddress mac;
};


struct NetworkInfo
{
	MacAddress mac;
	IPv4Config config;
    bool dhcp;
};

#endif /* NETWORK_HPP_ */
