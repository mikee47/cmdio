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

#include "SmingCore.h"
#include <ESP8266LLMNR/ESP8266LLMNR.h>

#include <JsonConfigFile.h>
#include "TimeManager.h"
#include "NetworkManager.h"
#include "FileManager.h"

// Global instance
NetworkManager networkManager;

// Local Link Multicast Name Resolution (windows)
static LLMNRResponder LLMNR;

// DNS parameters
static const uint8_t DNS_PORT = 53;

// Information broadcast via ZeroConf (LLMNR, MDNS)
static const char MDNS_SERVER_NAME[] = "stslc";
static const char MDNS_VERSION[] = "version = now";  //stslc_0.1

// System config file (private - secure)
static DEFINE_STRING_P(FILE_NETWORK_CONFIG, ".network.json")
static DEFINE_STRING_P(ATTR_HOSTNAME, "hostname")
static DEFINE_STRING_P(ATTR_SERVER_PORT, "server-port")
static DEFINE_STRING_P(DEFAULT_HOSTNAME, "lightcon")
static const uint16_t DEFAULT_SERVER_PORT = 80;

// WiFi details
static DEFINE_STRING_P(CONFIG_AP, "accesspoint")
static DEFINE_STRING_P(ATTR_SSID, "ssid")
DEFINE_STRING_P(ATTR_PASSWORD, "password")
static DEFINE_STRING_P(DEFAULT_AP_SSID, "STS Lightcon")
static DEFINE_STRING_P(DEFAULT_AP_PASSWORD, "sts welcome")
static DEFINE_STRING_P(ATTR_STATION, "station")

// WiFi scan information
static DEFINE_STRING_P(ATTR_NETWORKS, "networks")
static DEFINE_STRING_P(ATTR_BSSID, "bssid")
static DEFINE_STRING_P(ATTR_AUTH, "auth")
static DEFINE_STRING_P(ATTR_CHANNEL, "channel")
static DEFINE_STRING_P(ATTR_RSSI, "rssi")
static DEFINE_STRING_P(ATTR_HIDDEN, "hidden")
static DEFINE_STRING_P(ATTR_SIMPLEPAIR, "simplepair")

// Commands
static DEFINE_STRING_P(METHOD_NETWORK, "network")
static DEFINE_STRING_P(COMMAND_SCAN, "scan")
static DEFINE_STRING_P(COMMAND_CONFIG, "config")
static DEFINE_STRING_P(ATTR_MACADDR, "MAC Address")
static DEFINE_STRING_P(ATTR_IPADDR, "IP Address")

// WiFi event names
#if DEBUG_BUILD
static DEFINE_STRING_P(STAMODE_CONNECTED, "STAMODE_CONNECTED")
;
static DEFINE_STRING_P(STAMODE_DISCONNECTED, "STAMODE_DISCONNECTED")
;
static DEFINE_STRING_P(STAMODE_AUTHMODE_CHANGE, "STAMODE_AUTHMODE_CHANGE")
;
static DEFINE_STRING_P(STAMODE_GOT_IP, "STAMODE_GOT_IP")
;
static DEFINE_STRING_P(STAMODE_DHCP_TIMEOUT, "STAMODE_DHCP_TIMEOUT")
;
static DEFINE_STRING_P(SOFTAPMODE_STACONNECTED, "SOFTAPMODE_STACONNECTED")
;
static DEFINE_STRING_P(SOFTAPMODE_STADISCONNECTED, "SOFTAPMODE_STADISCONNECTED")
;
static DEFINE_STRING_P(SOFTAPMODE_PROBEREQRECVED, "SOFTAPMODE_PROBEREQRECVED")
;
#endif

String macToStr(uint8_t hwaddr[6])
{
	return toHexString(hwaddr, 6, ':');
}

String authModeToStr(AUTH_MODE mode)
{
	switch (mode) {
	case AUTH_OPEN:
		return F("OPEN");
	case AUTH_WEP:
		return F("WEP");
	case AUTH_WPA_PSK:
		return F("WPA_PSK");
	case AUTH_WPA2_PSK:
		return F("WPA2_PSK");
	case AUTH_WPA_WPA2_PSK:
		return F("WPA_WPA2_PSK");
	default:
		return String(mode);
	}
}

static DEFINE_STRING_P(STR_REASON, "REASON_")
;

/*
 * Disconnect message
 */
static String reasonToStr(uint8_t reason)
{
#define XX(_tag) \
  case REASON_ ## _tag: \
    return String_P(PSTR(#_tag));

	switch (reason) {
	XX(UNSPECIFIED)
			XX(AUTH_EXPIRE)
			XX(AUTH_LEAVE)
			XX(ASSOC_EXPIRE)
			XX(ASSOC_TOOMANY)
			XX(NOT_AUTHED)
			XX(NOT_ASSOCED)
			XX(ASSOC_LEAVE)
			XX(ASSOC_NOT_AUTHED)
			XX(DISASSOC_PWRCAP_BAD)
			XX(DISASSOC_SUPCHAN_BAD)
			XX(IE_INVALID)
			XX(MIC_FAILURE)
			XX(4WAY_HANDSHAKE_TIMEOUT)
			XX(GROUP_KEY_UPDATE_TIMEOUT)
			XX(IE_IN_4WAY_DIFFERS)
			XX(GROUP_CIPHER_INVALID)
			XX(PAIRWISE_CIPHER_INVALID)
			XX(AKMP_INVALID)
			XX(UNSUPP_RSN_IE_VERSION)
			XX(INVALID_RSN_IE_CAP)
			XX(802_1X_AUTH_FAILED)
			XX(CIPHER_SUITE_REJECTED)
			XX(BEACON_TIMEOUT)
			XX(NO_AP_FOUND)
			XX(AUTH_FAIL)
			XX(ASSOC_FAIL)
			XX(HANDSHAKE_TIMEOUT)
			default:
		return STR_REASON() + String(reason);
	}
#undef XX
}

//mDNS using ESP8266 SDK functions
void NetworkManager::startMDNS()
{
	// Windows uses this one
	if (LLMNR.begin(m_hostname))
		debug_i("LLMNR responder started");
	else
		debug_e("LLMNR responder failed to start");

#ifdef ENABLE_ESPCONN
	struct mdns_info info = {
		.host_name = m_hostname.begin(),
		.server_name = (char*)MDNS_SERVER_NAME,
		.server_port = m_serverPort,
		.ipAddr = WifiStation.getIP(),
		.txt_data = {(char*)MDNS_VERSION}
	};
	espconn_mdns_init(&info);
#endif
}

void NetworkManager::onStatusChange(network_callback_t callback)
{
	m_onStatusChange = callback;
}

void NetworkManager::wifiEventHandler(System_Event_t *evt)
{
	switch (evt->event)
	{
	case EVENT_STAMODE_CONNECTED: {
#if DEBUG_BUILD
		Event_StaMode_Connected_t& e = evt->event_info.connected;
		debug_i("%s('%s', #%d)", STAMODE_CONNECTED().c_str(), e.ssid, e.channel);
#endif
		if (m_configConnection)
			configComplete(0);
		break;
	}

	case EVENT_STAMODE_DISCONNECTED: {
		Event_StaMode_Disconnected_t& e = evt->event_info.disconnected;
#if DEBUG_BUILD
		debug_i("%s('%s', %s, %u %s)", STAMODE_DISCONNECTED().c_str(), e.ssid, macToStr(e.bssid).c_str(), e.reason, reasonToStr(e.reason).c_str());
#endif
		statusChanged(nwc_disconnected);
		if (m_configConnection)
			configComplete(e.reason);
		break;
	}

	case EVENT_STAMODE_AUTHMODE_CHANGE: {
#if DEBUG_BUILD
		Event_StaMode_AuthMode_Change_t& e = evt->event_info.auth_change;
		debug_i("%s(%d, %d)", STAMODE_AUTHMODE_CHANGE().c_str(), e.old_mode, e.new_mode);
#endif
		break;
	}

	case EVENT_STAMODE_GOT_IP: {
		// WiFi station now operational
#if DEBUG_BUILD
		Event_StaMode_Got_IP_t& e = evt->event_info.got_ip;
		debug_i("%s(%s)", STAMODE_GOT_IP().c_str(), IPAddress(e.ip).toString().c_str());
#endif
		statusChanged(nwc_connected);
		startMDNS();
		ntpInit();
		break;
	}

	case EVENT_SOFTAPMODE_STACONNECTED: {
#if DEBUG_BUILD
		Event_SoftAPMode_StaConnected_t& e = evt->event_info.sta_connected;
		debug_i("%s(%s, %u)", SOFTAPMODE_STACONNECTED().c_str(), macToStr(e.mac).c_str(), e.aid);
#endif
		break;
	}

	case EVENT_SOFTAPMODE_STADISCONNECTED: {
#if DEBUG_BUILD
		Event_SoftAPMode_StaDisconnected_t& e = evt->event_info.sta_disconnected;
		debug_i("%s(%s, %u)", SOFTAPMODE_STADISCONNECTED().c_str(), macToStr(e.mac).c_str(), e.aid);
#endif
		break;
	}

	case EVENT_SOFTAPMODE_PROBEREQRECVED: {
#if DEBUG_BUILD
//    Event_SoftAPMode_ProbeReqRecved_t& e = evt->event_info.ap_probereqrecved;
//    debug_i("%s(%u, %s)", SOFTAPMODE_PROBEREQRECVED().c_str(), e.rssi, macToStr(e.mac).c_str());
#endif
		break;
	}

	default:
		;
	}

}

/*
 * Enable/disable AP mode. Station mode remains active so we can scan,
 * but is disassociated from any AP.
 */
bool NetworkManager::accessPointMode(bool enable)
{
	if (m_dnsServer) {
		delete m_dnsServer;
		m_dnsServer = nullptr;
	}

//  WifiStation.enable(true);
	WifiStation.enable(false);

	bool ret = true;
	if (enable) {
		wifi_info_t info;
		{
			JsonConfigFile config;
			config.load(FILE_NETWORK_CONFIG());
			JsonObject& ap = config[CONFIG_AP()];
			info.ssid = ap[ATTR_SSID()].asString() ? : DEFAULT_AP_SSID();
			info.password = ap[ATTR_PASSWORD()].asString() ? : DEFAULT_AP_PASSWORD();
		}

		wifi_station_set_reconnect_policy(false);
		WifiStation.disconnect();
		WifiAccessPoint.enable(true);
		ret = WifiAccessPoint.config(info.ssid, info.password, info.password.length() ? AUTH_WPA2_PSK : AUTH_OPEN);

		m_dnsServer = new DNSServer();
		if (m_dnsServer)
			m_dnsServer->start(DNS_PORT, "*", WifiAccessPoint.getIP());

		debug_i("AP mode, SSID '%s' - %s", WifiAccessPoint.getSSID().c_str(), ret ? "OK" : "FAILED");
	}
	else {
		WifiAccessPoint.enable(false);
		WifiStation.enable(true);
		WifiStation.connect();
		wifi_station_set_reconnect_policy(true);
		debug_i("AP mode disabled");
	}

	statusChanged(WifiAccessPoint.isEnabled() ? nwc_apEnabled : nwc_apDisabled);
	return ret;
}

void NetworkManager::configure(command_connection_t connection, JsonObject& json)
{
	// If connection's been dropped we can continue
	if (m_configConnection)
		if (!m_configConnection->active())
			m_configConnection = nullptr;

	// Already configuring ?
	if (m_configConnection) {
		if (m_configConnection == connection) {
			setPending(json);
			return;
		}
		// Already reconfiguring via different connection
		setError(json, ioe_access_denied);
		return;
	}

	// Hostname is an optional parameter
	const char* hostname = json[ATTR_HOSTNAME()];
	if (hostname) {
		if (WifiStation.getHostname() != hostname) {
			m_hostname = hostname;
			WifiStation.setHostname(m_hostname);
			JsonConfigFile config;
			config.load(FILE_NETWORK_CONFIG());
			config[ATTR_HOSTNAME()] = m_hostname;
			config.save(FILE_NETWORK_CONFIG());
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
	wifi_info_t* info = new wifi_info_t;
	if (!info) {
		setError(json, ioe_nomem);
		return;
	}
	info->ssid = json[ATTR_SSID()].asString();
	info->password = json[ATTR_PASSWORD()].asString();
	// Don't send password back in response
	json.remove(ATTR_PASSWORD());

	m_configConnection = connection;

	auto callback = [](os_param_t param) {
		auto info = reinterpret_cast<wifi_info_t*>(param);
		// The settings are saved by the ESP8266 firmware
//    WifiAccessPoint.enable(false);
		if (WifiStation.isConnected())
		WifiStation.disconnect();
		else
		WifiStation.enable(true);
		bool res = WifiStation.config(info->ssid, info->password);
		delete info;
		if (res)
		res = WifiStation.connect();

		if (res) {
			debug_i("Hostname '%s' connecting to SSID '%s'", WifiStation.getHostname().c_str(), WifiStation.getSSID().c_str());
			/*
			 * We now wait for connection
			 */
		}
		else {
			debug_w("Station config failed");
			networkManager.configComplete(REASON_UNSPECIFIED);
		}

		networkManager.statusChanged(nwc_configChanged);
	};

	System.deferCallback(callback, reinterpret_cast<os_param_t>(info));

	setPending(json);
}

/*
 * Notify client of result of configuration.
 *
 * @param errReason System-defined error code, 0 on success.
 */
void NetworkManager::configComplete(uint8_t errReason)
{
	if (errReason)
		WifiStation.disconnect();

	// Low-value reasons are 'internal' values so we only send the final result to the client
//  if (reason < REASON_BEACON_TIMEOUT)
//    return;

	if (m_configConnection) {
		DynamicJsonBuffer buffer;
		JsonObject& json = buffer.createObject();
		json[ATTR_METHOD()] = METHOD_NETWORK();
		json[ATTR_COMMAND()] = COMMAND_CONFIG();
		if (errReason)
			setError(json, errReason, reasonToStr(errReason));
		else
			setSuccess(json);

		m_configConnection->send(json);

		if (!errReason)
			m_configConnection = nullptr;
	}

	if (!errReason && WifiAccessPoint.isEnabled())
		System.deferCallback([](os_param_t) {
			networkManager.accessPointMode(false);
		});
}

void NetworkManager::scanComplete(void* arg, STATUS status)
{
	if (status != OK)
		debug_w("Network scan failed (%u)", status);

	if (!m_scanConnection)
		return;

	DynamicJsonBuffer buffer;
	JsonObject& json = buffer.createObject();
	json[ATTR_METHOD()] = METHOD_NETWORK();
	json[ATTR_COMMAND()] = COMMAND_SCAN();

	if (status != OK) {
		setError(json, status);
	}
	else {
		setSuccess(json);
		JsonArray& networks = json.createNestedArray(ATTR_NETWORKS());

		BssInfoEnum bss;
		if (bss.init(static_cast<bss_info*>(arg)))
			do {
				JsonObject& nw = networks.createNestedObject();
				nw[ATTR_SSID()] = bss.SSID();
				nw[ATTR_BSSID()] = bss.BSSID();
				nw[ATTR_AUTH()] = bss.authModeStr();
				nw[ATTR_CHANNEL()] = bss.channel();
				nw[ATTR_RSSI()] = bss.rssi();
				nw[ATTR_HIDDEN()] = bss.hidden();
				nw[ATTR_SIMPLEPAIR()] = bss.simplePair();
			} while (bss.next());
	}

	m_scanConnection->send(json);
	m_scanConnection = nullptr;
}

void NetworkManager::scan(command_connection_t connection, JsonObject& json)
{
	// Scan in progress ?
	if (m_scanConnection) {
		setError(json);
		return;
	}

	WifiStation.enable(true);
	auto cb = [](void* arg, STATUS status) {
		networkManager.scanComplete(arg, status);
	};
	if (!wifi_station_scan(NULL, cb)) {
		setError(json);
		return;
	}

	m_scanConnection = connection;
	setPending(json);
}

NetworkManager::NetworkManager() :
m_ntpClient((NtpTimeResultDelegate)[](NtpClient& client, time_t timestamp) {
		timeManager.update(timestamp);
	})
{
}

void NetworkManager::begin()
{
	wifi_set_event_handler_cb([](System_Event_t* evt) {
		networkManager.wifiEventHandler(evt);
	});

	WifiAccessPoint.enable(false);
	WifiStation.enable(true);

	{
		JsonConfigFile config;
		config.load(FILE_NETWORK_CONFIG());
		m_hostname = config[ATTR_HOSTNAME()].asString() ? : DEFAULT_HOSTNAME();
		WifiStation.setHostname(m_hostname);
		m_serverPort = config[ATTR_SERVER_PORT()].as<uint16_t>() ? : DEFAULT_SERVER_PORT;

		JsonObject& station = config[ATTR_STATION()];
		if (station.success())
			configure(nullptr, station);
	}
}

String NetworkManager::getMethod() const
{
	return METHOD_NETWORK();
}

void NetworkManager::handleMessage(command_connection_t connection, JsonObject& json)
{
	const char* command = json[ATTR_COMMAND()];

	if (COMMAND_INFO() == command) {
		json[ATTR_MACADDR()] = WifiStation.getMAC();
		json[ATTR_IPADDR()] = WifiStation.getIP().toString();
		json[ATTR_SSID()] = WifiStation.getSSID();
		return;
	}

	if (COMMAND_SCAN() == command) {
		scan(connection, json);
		return;
	}

	if (COMMAND_CONFIG() == command) {
		configure(connection, json);
		return;
	}
}

void NetworkManager::ntpInit()
{
//  m_ntpClient.setNtpServer(NTP_SERVER());
	m_ntpClient.setAutoQuery(true);
	m_ntpClient.requestTime();
}

void NetworkManager::onNtpReceive(NtpClient& client, time_t timestamp)
{
	debug_i("%s(%u)", __FUNCTION__, timestamp);
	timeManager.update(timestamp);
}

