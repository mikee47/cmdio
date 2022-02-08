/*
 * NetworkManager.h
 *
 *  Created on: 28 May 2018
 *      Author: mikee47
 */

#pragma once

#include "CommandHandler.h"
#include <Network/Mdns/Responder.h>
#include <Network/DnsServer.h>
#include <Network/NtpClient.h>
#include <Platform/AccessPoint.h>
#include <Platform/Station.h>
#include <Platform/WifiEvents.h>
#include <WString.h>

DECLARE_FSTR(ATTR_PASSWORD)
DECLARE_FSTR(COMMAND_DISCOVER);

/*
 * Simple config structure for wifi ap or station
 */
struct wifi_info_t {
	String ssid;
	String password;
};

enum network_change_t {
	// Disconnected from AP
	nwc_disconnected,
	// Connected to AP, address assigned
	nwc_connected,
	//Access Point mode enabled
	nwc_apEnabled,
	// Access Point mode disabled
	nwc_apDisabled,
	// Configuration changed
	nwc_configChanged,
	// System clock time updated via NTP
	nwc_timeUpdated,
	// Discovery requested
	nwc_discover,
};

typedef void (*network_callback_t)(network_change_t nwc);

typedef void (*network_scan_complete_t)(JsonObject json, void* param);

class NetworkManager : public WSCommandHandler
{
public:
	~NetworkManager()
	{
		delete ntpClient;
	}

	void begin();

	void onStatusChange(network_callback_t callback)
	{
		statusChangeCallback = callback;
	}

	void scan(WSCommandConnection* connection, JsonObject json);

	void configure(WSCommandConnection* connection, JsonObject json);

	bool accessPointMode(bool enable);

	uint16_t webServerPort() const
	{
		return WifiAccessPoint.isEnabled() ? 80 : serverPort;
	}

	/* WSCommandHandler */

	String getMethod() const override;

	UserRole getMinAccess() const override
	{
		return UserRole::User;
	}

	void handleMessage(WSCommandConnection* connection, JsonObject json) override;

private:
	void startMDNS();
	void ntpInit();

	void onNtpReceive(NtpClient& client, time_t timestamp);

	void configComplete(WifiDisconnectReason reason);

	void scanComplete(bool success, BssList& list);
	bool stationMode(const wifi_info_t& info);
	void setEventHandlers();

	void statusChanged(network_change_t nwc)
	{
		if(statusChangeCallback) {
			statusChangeCallback(nwc);
		}
	}

private:
	MacAddress originalMac;
	// Only need this in AP mode so create it dynamically
	DnsServer* dnsServer = nullptr;
	// Persistent data for MDNS - libraries don't reliably keep copies
	String hostName;
	//
	uint16_t serverPort = 80;
	// The client connection being used to reconfigure network
	WSCommandConnection* configConnection = nullptr;
	//
	network_callback_t statusChangeCallback = nullptr;
	// Network scan
	WSCommandConnection* scanConnection = nullptr;
	// For keeping system clock accurate
	NtpClient* ntpClient = nullptr;
	//
	mDNS::Responder mdnsResponder;
};

extern NetworkManager networkManager;
