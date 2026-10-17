#pragma once

#include <cstdint>
#include <cstring>

#include "Transport.hpp"
#include "GpioPin.hpp"

extern void print(const char* buffer);

extern "C"
{
int _write(int /*fd*/, unsigned char *buf, int len)
{
    CDC_Transmit_FS(buf, len);
    return len;
}
}

extern "C"
{
#include "wizchip_conf.h"
#include "socket.h"
#include "dns.h"
#include "dhcp.h"
}

struct IPv4Address
{
    uint8_t octet[4];
};

struct MacAddress
{
    uint8_t heptet[6];
};

struct NetworkInfo
{
	MacAddress mac;

    IPv4Address ip;
    IPv4Address subnet;
    IPv4Address gateway;
    IPv4Address dns;

    bool dhcp = false;
};

template <typename Transport, typename ResetPin, auto &NetworkInfo>
class W5500Driver
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
    explicit W5500Driver(Transport &transport) : transport_(transport), reset_pin_{}
    {
    	active_ = this;
    	ip_assigned_ = false;
    }


    int initialize(void)
    {
    	print("initializing WIZCHIP\r\n");
    	printf("initializing WIZCHIP\r\n");
    	uint8_t memsize[2][8] = {{2,2,2,2,2,2,2,2},{2,2,2,2,2,2,2,2}};

        /***** Reset Sequence  *****/
        reset_pin_.low();
        HAL_Delay(50);
        reset_pin_.high();
        HAL_Delay(200);

        /***** Register callbacks  *****/
        registerCallbacks();

        /***** Initialize the chip  *****/
        if (ctlwizchip(CW_INIT_WIZCHIP, (void*)memsize) == -1){
        	printf("Error while initializing WIZCHIP\r\n");
        	return -1;
        }
        printf("WIZCHIP Initialized\r\n");

        /***** check communication by reading Version  *****/
        uint8_t ver = getVERSIONR();
        if (ver != 0x04){
        	printf("Error Communicating with W5500\t Version: 0x%02X\r\n", ver);
        	return -2;
        }
        printf("Checking Link Status..\r\n");

     	/*****  CHeck Link Status  *****/
        uint8_t link = PHY_LINK_OFF;
        uint8_t retries = 10;
        while ((link != PHY_LINK_ON) && (retries > 0)){
            ctlwizchip(CW_GET_PHYLINK, &link);
            if (link == PHY_LINK_ON) printf("Link: UP\r\n");
            else printf("Link: DOWN Retrying : %d\r\n", 10-retries);
            retries--;
            HAL_Delay(500);
        }
        if (link != PHY_LINK_ON){
        	printf ("Link is Down,please reconnect and retry\nExiting Setup..\r\n");
        	return 3;
        }

        /***** Use DHCP or Static IP  *****/

        if (NetworkInfo.dhcp)
        	get_dynamic_address();
        else
        	assign_static_address();

        /***** Configure DNS  *****/
        HAL_Delay(500);
        printf("Configuring DNS..\r\n");

        uint8_t DNS_buffer[512];
        DNS_init(static_cast<uint8_t>(DNS_SOCKET), DNS_buffer);

        /***** Print assigned IP on the console  *****/
        wiz_NetInfo tmpInfo;
        ctlnetwork(CN_GET_NETINFO, &tmpInfo);
        printf("IP: %d.%d.%d.%d\r\n", tmpInfo.ip[0], tmpInfo.ip[1], tmpInfo.ip[2], tmpInfo.ip[3]);
        printf("SUBNET: %d.%d.%d.%d\r\n", tmpInfo.sn[0], tmpInfo.sn[1], tmpInfo.sn[2], tmpInfo.sn[3]);
        printf("GATEWAY: %d.%d.%d.%d\r\n", tmpInfo.gw[0], tmpInfo.gw[1], tmpInfo.gw[2], tmpInfo.gw[3]);
        printf("DNS: %d.%d.%d.%d\r\n", tmpInfo.dns[0], tmpInfo.dns[1], tmpInfo.dns[2], tmpInfo.dns[3]);

        return 0;
    }



    bool initialize_()
    {
        registerCallbacks();

        uint8_t tx_mem[8] = {2, 2, 2, 2, 2, 2, 2, 2};
        uint8_t rx_mem[8] = {2, 2, 2, 2, 2, 2, 2, 2};

        return wizchip_init(tx_mem, rx_mem) == 0;
    }

    wiz_NetInfo convertToWiz()
    {
        wiz_NetInfo net{};

        memcpy(net.mac, &NetworkInfo.mac, 6);
        memcpy(net.ip,  &NetworkInfo.ip.octet,      4);
        memcpy(net.sn,  &NetworkInfo.subnet.octet,  4);
        memcpy(net.gw,  &NetworkInfo.gateway.octet, 4);
        memcpy(net.dns, &NetworkInfo.dns.octet,     4);
        net.dhcp = NETINFO_STATIC;
        return net;
    }

    bool assign_static_address()
    {
        auto net = convertToWiz();
        return ctlnetwork(CN_SET_NETINFO, &net);
    }

    bool get_dynamic_address()
    {
        printf ("Using DHCP.. Please Wait..\r\n");
        wiz_NetInfo netInfo{};
        setSHAR(netInfo.mac);
        uint8_t DHCP_buffer[548];
        DHCP_init(static_cast<uint8_t>(DHCP_SOCKET), DHCP_buffer);

        reg_dhcp_cbfunc(&Callback_IPAssigned, &Callback_IPAssigned, &Callback_IPConflict);

        uint8_t retries = 20;
        while((!ip_assigned_) && (retries > 0)) {
            DHCP_run();
            HAL_Delay(500);
            retries--;
        }
        if(!ip_assigned_) {
        	// DHCP Failed, switch to static IP
        	printf ("DHCP Failed, switching to static IP\r\n");
        	return ctlnetwork(CN_SET_NETINFO, (void*)&netInfo);
        }
        else {
        	// if IP is allocated, read it
            getIPfromDHCP(netInfo.ip);
            getGWfromDHCP(netInfo.gw);
            getSNfromDHCP(netInfo.sn);
            getDNSfromDHCP(netInfo.dns);

            // Now apply them to the chip
            printf("DHCP IP assigned successfully\r\n");
            return ctlnetwork(CN_SET_NETINFO, (void*)&netInfo);
        }
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

    bool openUdp(SOCKET_NUMBER socket_number, uint16_t local_port)
    {

    	printf("W5500Driver::openUdp before closeUdp\n");
    	closeUdp(socket_number);
    	printf("W5500Driver::openUdp after closeUdp\n");

        auto ret = socket(socket_number, Sn_MR_UDP, local_port, 0) == socket_number;
    	printf("W5500Driver::openUdp after socket\n");
    	return ret;
    }

    void closeUdp(SOCKET_NUMBER socket_number)
    {
        closesocket(static_cast<uint8_t>(socket_number));
    }

    bool openTcp(SOCKET_NUMBER socket_number, uint16_t local_port)
    {
        closeTcp(socket_number);

        return socket(sockestatic_cast<uint8_t>(socket_number), Sn_MR_TCP, local_port, 0) == static_cast<uint8_t>(socket_number);
    }

    void closeTcp(SOCKET_NUMBER socket_number)
    {
        closesocket(static_cast<uint8_t>(socket_number));
    }

    bool connectTcp(SOCKET_NUMBER socket_number, const IPv4Address& remote_address, uint16_t remote_port)
    {
        return ::connect(static_cast<uint8_t>(socket_number), const_cast<uint8_t*>(remote_address.octet), remote_port) == SOCK_OK;
    }

    bool disconnectTcp(SOCKET_NUMBER socket_number)
    {
        return ::disconnect(static_cast<uint8_t>(socket_number)) == SOCK_OK;
    }

    bool listenTcp(SOCKET_NUMBER socket_number)
    {
        return ::listen(static_cast<uint8_t>(socket_number)) == SOCK_OK;
    }

    int32_t sendTcp(SOCKET_NUMBER socket_number, const uint8_t* data, uint16_t size)
    {
        return send(static_cast<uint8_t>(socket_number), const_cast<uint8_t*>(data), size);
    }

    int32_t receiveTcp(SOCKET_NUMBER socket_number, uint8_t* data, uint16_t size)
    {
        return recv(static_cast<uint8_t>(socket_number), data,size);
    }

    uint8_t socketStatus(SOCKET_NUMBER socket_number) const
    {
        return getSn_SR(static_cast<uint8_t>(socket_number));
    }

    uint16_t rxAvailable(SOCKET_NUMBER socket_number) const
    {
        return getSn_RX_RSR(static_cast<uint8_t>(socket_number));
    }

    uint16_t txAvailable(SOCKET_NUMBER socket_number) const
    {
        return getSn_TX_FSR(static_cast<uint8_t>(socket_number));
    }

    int32_t sendUdp(SOCKET_NUMBER socket_number, const uint8_t *data, uint16_t size, const IPv4Address &destination, uint16_t destination_port)
    {
        return sendto(static_cast<uint8_t>(socket_number), const_cast<uint8_t *>(data), size, const_cast<uint8_t *>(destination.octet), destination_port);
    }

    int32_t receiveUdp(SOCKET_NUMBER socket_number, uint8_t *data, uint16_t size, IPv4Address &source, uint16_t &source_port)
    {
        return recvfrom(static_cast<uint8_t>(socket_number), data, size, source.octet, &source_port);
    }

public:
    static void wizchipSelect()
    {
        active_->transport_.select();
    }

    static void wizchipDeselect()
    {
    	active_->transport_.deselect();
    }

    static uint8_t wizchipReadByte()
    {
        uint8_t rx = 0;
        active_->transport_.read_nocs(&rx, 1);
        return rx;
    }

    static void wizchipWriteByte(uint8_t data)
    {
    	active_->transport_.write_nocs(&data, 1);
    }

    static void wizchipReadBurst(uint8_t *buffer, uint16_t length)
    {
    	active_->transport_.read_nocs(buffer, length);
    }

    static void wizchipWriteBurst(uint8_t *buffer, uint16_t length)
    {
    	active_->transport_.write_nocs(buffer, length);
    }

    static void Callback_IPAssigned(void) {
	    ip_assigned_ = true;
	}

	static void Callback_IPConflict(void) {
	    ip_assigned_ = false;
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
    ResetPin reset_pin_;
    static inline volatile bool ip_assigned_ = false;
    static inline W5500Driver *active_ = nullptr;

};

// static_assert(W5500DriverUdpConcept<W5500Driver<MyTransport>>);

// static_assert(W5500DriverTcpConcept<W5500Driver<MyTransport>>);
