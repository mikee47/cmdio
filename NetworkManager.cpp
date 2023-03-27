/*
 * network.cpp
 *
 *  Created on: 28 May 2018
 *      Author: mikee47
 *
 * ESP8266 doesn't have an RTC and will drift, so we use NTP to keep time roughly accurate.
 *
 * TODO:
 *
 *  Track next dawn/dusk times. These are calculated as times from midnight but we'll
 *  store them as full time_t values for easy comparison with timers.
 *
 */

#include <ESP8266LLMNR/ESP8266LLMNR.h>
#include <core/TimeManager.h>
#include "NetworkManager.h"
#include "FileManager.h"

#ifndef ARCH_ESP8266
inline void wifi_station_set_reconnect_policy(bool)
{
}
#endif

// Global instance
NetworkManager networkManager;

// Local Link Multicast Name Resolution (windows)
static LLMNRResponder LLMNR;

// DNS parameters
static const uint8_t DNS_PORT = 53;

// System config file (private - secure)
DEFINE_FSTR_LOCAL(FILE_NETWORK_CONFIG, "config/.network.json");
DEFINE_FSTR_LOCAL(ATTR_HOSTNAME, "hostname");
DEFINE_FSTR_LOCAL(ATTR_SERVER_PORT, "server-port");
DEFINE_FSTR_LOCAL(DEFAULT_HOSTNAME, "sming-demo");
static const uint16_t DEFAULT_SERVER_PORT = 80;

// WiFi details
DEFINE_FSTR_LOCAL(CONFIG_AP, "accesspoint");
DEFINE_FSTR_LOCAL(ATTR_SSID, "ssid");
DEFINE_FSTR_LOCAL(ATTR_BSSID, "bssid");
DEFINE_FSTR(ATTR_PASSWORD, "password");
DEFINE_FSTR_LOCAL(DEFAULT_AP_SSID, "Sming IFS Demo");
DEFINE_FSTR_LOCAL(DEFAULT_AP_PASSWORD, "welcome");

// WiFi scan information
DEFINE_FSTR_LOCAL(ATTR_NETWORKS, "networks");
DEFINE_FSTR_LOCAL(ATTR_AUTH, "auth");
DEFINE_FSTR_LOCAL(ATTR_CHANNEL, "channel");
DEFINE_FSTR_LOCAL(ATTR_RSSI, "rssi");
DEFINE_FSTR_LOCAL(ATTR_HIDDEN, "hidden");

// Commands
DEFINE_FSTR_LOCAL(METHOD_NETWORK, "network");
DEFINE_FSTR_LOCAL(COMMAND_SCAN, "scan");
DEFINE_FSTR_LOCAL(COMMAND_CONFIG, "config");
DEFINE_FSTR_LOCAL(ATTR_MACADDR, "MAC");
DEFINE_FSTR_LOCAL(ATTR_IPADDR, "IP");
DEFINE_FSTR(COMMAND_DISCOVER, "discover");

//mDNS using ESP8266 SDK functions
void NetworkManager::startMDNS()
{
	// Windows uses this one
	if(LLMNR.begin(hostName)) {
		debug_i("LLMNR responder started");
	} else {
		debug_e("LLMNR responder failed to start");
	}

	if(mdnsResponder.begin(hostName)) {
		debug_i("MDNS responder started");
	} else {
		debug_e("MDNS responder failed to start");
	}
}

void NetworkManager::setEventHandlers()
{
	WifiEvents.onStationConnect([this](const String& ssid, MacAddress bssid, uint8_t channel) {
#if DEBUG_BUILD
		debug_i("StationConnect('%s', %s, #%d)", ssid.c_str(), bssid.toString().c_str(), channel);
#endif
		if(configConnection != nullptr) {
			configComplete(WifiDisconnectReason(0));
		}
	});

	WifiEvents.onStationDisconnect([this](const String& ssid, MacAddress bssid, WifiDisconnectReason reason) {
#if DEBUG_BUILD
		debug_i("StationDisconnect('%s', %s, %u %s)", ssid.c_str(), bssid.toString().c_str(), reason,
				WifiEvents.getDisconnectReasonName(reason).c_str());
#endif
		statusChanged(nwc_disconnected);
		if(configConnection != nullptr) {
			configComplete(reason);
		}
	});

	WifiEvents.onStationAuthModeChange([this](WifiAuthMode oldMode, WifiAuthMode newMode) {
#if DEBUG_BUILD
		debug_i("StationAuthModeChange(%d, %d)", oldMode, newMode);
#endif
	});

	WifiEvents.onStationGotIP([this](IpAddress ip, IpAddress netmask, IpAddress gateway) {
	// WiFi station now operational
#if DEBUG_BUILD
		debug_i("StationGotIP(%s)", ip.toString().c_str());
#endif
		statusChanged(nwc_connected);
		startMDNS();
		ntpInit();
	});

#if DEBUG_BUILD
	WifiEvents.onAccessPointConnect(
		[](MacAddress mac, uint16_t aid) { debug_i("AccessPointConnect(%s, %u)", mac.toString().c_str(), aid); });

	WifiEvents.onAccessPointDisconnect(
		[](MacAddress mac, uint16_t aid) { debug_i("AccessPointDisconnect(%s, %u)", mac.toString().c_str(), aid); });

/*
	WifiEvents.onAccessPointProbeReqRecved([](int rssi, MacAddress mac)
    {
		debug_i("AccessPointProbeReqRecved(%u, %s)", rssi, mac.toString().c_str());
    });
*/
#endif
}

/*
 * Enable/disable AP mode. Station mode remains active so we can scan,
 * but is disassociated from any AP.
 */
bool NetworkManager::accessPointMode(bool enable)
{
	dnsServer.reset();

	//  WifiStation.enable(true);
	WifiStation.enable(false);

	bool ret = true;
	if(enable) {
		wifi_info_t info;
		{
			DynamicJsonDocument config(1024);
			Json::loadFromFile(config, FILE_NETWORK_CONFIG);
			JsonObject ap = config[CONFIG_AP];
			info.ssid = ap[ATTR_SSID] | String(DEFAULT_AP_SSID);
			info.password = ap[ATTR_PASSWORD] | String(DEFAULT_AP_PASSWORD);
		}

		wifi_station_set_reconnect_policy(false);
		WifiStation.disconnect();
		WifiAccessPoint.enable(true);
		ret = WifiAccessPoint.config(info.ssid, info.password, info.password.length() ? AUTH_WPA2_PSK : AUTH_OPEN);

		dnsServer.reset(new DnsServer);
		if(dnsServer) {
			dnsServer->start(DNS_PORT, "*", WifiAccessPoint.getIP());
		}

		debug_i("AP mode, SSID '%s' - %s", WifiAccessPoint.getSSID().c_str(), ret ? _F("OK") : _F("FAILED"));
	} else {
		WifiAccessPoint.enable(false);
		WifiStation.enable(true);
		WifiStation.connect();
		wifi_station_set_reconnect_policy(true);
		debug_i("AP mode disabled");
	}

	statusChanged(WifiAccessPoint.isEnabled() ? nwc_apEnabled : nwc_apDisabled);
	return ret;
}

void NetworkManager::configure(WSCommandConnection* connection, JsonObject json)
{
	// If connection's been dropped we can continue
	if(configConnection != nullptr) {
		if(!configConnection->active()) {
			configConnection = nullptr;
		}
	}

	// Already configuring ?
	if(configConnection != nullptr) {
		if(configConnection == connection) {
			IO::setPending(json);
			return;
		}
		// Already reconfiguring via different connection
		IO::setError(json, IO::Error::access_denied);
		return;
	}

	// Hostname is an optional parameter
	const char* configHostname = json[ATTR_HOSTNAME];
	if(configHostname != nullptr) {
		if(WifiStation.getHostname() != configHostname) {
			hostName = configHostname;
			WifiStation.setHostname(hostName);
			DynamicJsonDocument config(1024);
			Json::loadFromFile(config, FILE_NETWORK_CONFIG);
			config[ATTR_HOSTNAME] = hostName;
			Json::saveToFile(config, FILE_NETWORK_CONFIG);
		}
	}

	/*
	 * Don't reconfigure network now as response won't get through, so queue a task
	 * and notify a pending operation.
	 *
	 * The configuration may fail; assuming we're doing this from AP mode we want
	 * to keep the AP active until we've actually connected. At that point we notify
	 * successful connection and take the AP down. It's then up to the client to
	 * reconnect.
	 *
	 */
	auto cfg = new StationClass::Config{
		.ssid = json[ATTR_SSID].as<const char*>(),
		.password = json[ATTR_PASSWORD].as<const char*>(),
		.bssid = String(json[ATTR_BSSID].as<const char*>()),
#ifndef ARCH_ESP32 // Issue with this on ESP32...
		.autoConnectOnStartup = true,
#endif
		.save = true,
	};
	if(cfg == nullptr) {
		IO::setError(json, IO::Error::no_mem);
		return;
	}
	// Don't send password back in response
	json.remove(ATTR_PASSWORD);

	configConnection = connection;

	auto callback = [](os_param_t param) {
		auto cfg = reinterpret_cast<StationClass::Config*>(param);
		// The settings are saved by the ESP8266 firmware
		//    WifiAccessPoint.enable(false);
		if(WifiStation.isConnected()) {
			WifiStation.disconnect();
		} else {
			WifiStation.enable(true);
		}
		bool res = WifiStation.config(*cfg);
		delete cfg;
		if(res) {
			res = WifiStation.connect();
		}

		if(res) {
			debug_i("Hostname '%s' connecting to SSID '%s'", WifiStation.getHostname().c_str(),
					WifiStation.getSSID().c_str());
			/*
			 * We now wait for connection
			 */
		} else {
			debug_w("Station config failed");
			networkManager.configComplete(WIFI_DISCONNECT_REASON_UNSPECIFIED);
		}

		networkManager.statusChanged(nwc_configChanged);
	};

	System.queueCallback(callback, reinterpret_cast<os_param_t>(cfg));

	IO::setPending(json);
}

/*
 * Notify client of result of configuration.
 *
 * @param errReason System-defined error code, 0 on success.
 */
void NetworkManager::configComplete(WifiDisconnectReason errReason)
{
	if(errReason) {
		WifiStation.disconnect();
	}

	// Low-value reasons are 'internal' values so we only send the final result to the client
	//  if (reason < REASON_BEACON_TIMEOUT)
	//    return;

	if(configConnection) {
		DynamicJsonDocument doc(1024);
		auto json = doc.to<JsonObject>();
		json[ATTR_METHOD] = METHOD_NETWORK;
		json[ATTR_COMMAND] = String(COMMAND_CONFIG);
		if(errReason != 0) {
			IO::setError(json, errReason, WifiEvents.getDisconnectReasonName(errReason));
		} else {
			IO::setSuccess(json);
		}

		configConnection->send(json);

		if(!errReason) {
			configConnection = nullptr;
		}
	}

	if(!errReason && WifiAccessPoint.isEnabled())
		System.queueCallback([](os_param_t) { networkManager.accessPointMode(false); });
}

void NetworkManager::scanComplete(bool success, BssList& list)
{
	if(scanConnection == nullptr) {
		return;
	}

	DynamicJsonDocument doc(2048);
	auto json = doc.to<JsonObject>();
	json[ATTR_METHOD] = String(METHOD_NETWORK);
	json[ATTR_COMMAND] = String(COMMAND_SCAN);

	if(success) {
		IO::setSuccess(json);
		JsonArray networks = json.createNestedArray(ATTR_NETWORKS);

		for(unsigned i = 0; i < list.count(); ++i) {
			auto& bss = list[i];
			JsonObject nw = networks.createNestedObject();
			nw[ATTR_SSID] = bss.ssid;
			nw[ATTR_BSSID] = bss.bssid.toString();
			nw[ATTR_AUTH] = bss.getAuthorizationMethodName();
			nw[ATTR_CHANNEL] = bss.channel;
			nw[ATTR_RSSI] = bss.rssi;
			nw[ATTR_HIDDEN] = bss.hidden;
		}
	} else {
		IO::setError(json, IO::Error::timeout);
	}

	scanConnection->send(json);
	scanConnection = nullptr;
}

void NetworkManager::scan(WSCommandConnection* connection, JsonObject json)
{
	// Scan in progress ?
	if(scanConnection) {
		IO::setError(json, IO::Error::busy);
		return;
	}

	WifiStation.enable(true);
	if(!WifiStation.startScan(ScanCompletedDelegate(&NetworkManager::scanComplete, this))) {
		IO::setError(json, IO::Error::bad_command);
		return;
	}

	scanConnection = connection;
	IO::setPending(json);
}

void NetworkManager::begin()
{
	setEventHandlers();

	if(WifiStation.getSSID().length() == 0) {
		accessPointMode(true);
	} else {
		WifiAccessPoint.enable(false);
		WifiStation.enable(true);
		WifiStation.connect();
	}

	DynamicJsonDocument config(1024);
	Json::loadFromFile(config, FILE_NETWORK_CONFIG);
	originalMac = WifiStation.getMacAddress();
	String s = config[ATTR_MACADDR];
	MacAddress mac(s);
	if(mac) {
		WifiStation.setMacAddress(mac);
	}
	hostName = config[ATTR_HOSTNAME] | String(DEFAULT_HOSTNAME);
	WifiStation.setHostname(hostName);
	serverPort = config[ATTR_SERVER_PORT] | DEFAULT_SERVER_PORT;
}

String NetworkManager::getMethod() const
{
	return METHOD_NETWORK;
}

void NetworkManager::handleMessage(WSCommandConnection* connection, JsonObject json)
{
	const char* command = json[ATTR_COMMAND];

	if(COMMAND_INFO == command) {
		json[ATTR_MACADDR] = WifiStation.getMAC();
		json[ATTR_IPADDR] = WifiStation.getIP().toString();
		json[ATTR_SSID] = WifiStation.getSSID();
		json[ATTR_BSSID] = WifiStation.getBSSID().toString();
		IO::setSuccess(json);
		return;
	}

	if(COMMAND_SCAN == command) {
		scan(connection, json);
		return;
	}

	if(COMMAND_CONFIG == command) {
		configure(connection, json);
		return;
	}

	if(COMMAND_DISCOVER == command) {
		statusChanged(nwc_discover);
		IO::setSuccess(json);
		return;
	}
}

void NetworkManager::ntpInit()
{
	if(!ntpClient) {
		ntpClient.reset(new NtpClient(NtpTimeResultDelegate(&NetworkManager::onNtpReceive, this)));
	} else {
		ntpClient->requestTime();
	}
}

void NetworkManager::onNtpReceive(NtpClient& client, time_t timestamp)
{
	debug_i("NetworkManager::onNtpReceive(%u)", timestamp);
	timeManager.update(timestamp);
}
