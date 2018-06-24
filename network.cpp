/*
 * network.cpp
 *
 *  Created on: 28 May 2018
 *      Author: Mike
 */


#include <SmingCore/SmingCore.h>
#include <ESP8266LLMNR/ESP8266LLMNR.h>

#include <network.h>
#include <WString_P.h>
#include <apptasks.h>
#include <configfile.h>
#include <daylight.h>
#include <solarcalc.h>


// Global instance
CNetworkManager networkManager;

// DNS & AP parameters
static const uint8_t DNS_PORT = 53;
static IPAddress g_apIP(192, 168, 4, 1);

// Information broadcast via ZeroConf (LLMNR, MDNS)
static const char MDNS_SERVER_NAME[] = "stslc";
static const char MDNS_VERSION[] = "version = now"; //stslc_0.1


// System config file (private - secure)
static DEFINE_STRING_P(FILE_NETWORK_CONFIG, ".network.json")
static DEFINE_STRING_P(CONFIG_NETWORK, "network")
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
static DEFINE_STRING_P(STAMODE_CONNECTED, "STAMODE_CONNECTED");
static DEFINE_STRING_P(STAMODE_DISCONNECTED, "STAMODE_DISCONNECTED");
static DEFINE_STRING_P(STAMODE_AUTHMODE_CHANGE, "STAMODE_AUTHMODE_CHANGE");
static DEFINE_STRING_P(STAMODE_GOT_IP, "STAMODE_GOT_IP");
static DEFINE_STRING_P(STAMODE_DHCP_TIMEOUT, "STAMODE_DHCP_TIMEOUT");
static DEFINE_STRING_P(SOFTAPMODE_STACONNECTED, "SOFTAPMODE_STACONNECTED");
static DEFINE_STRING_P(SOFTAPMODE_STADISCONNECTED, "SOFTAPMODE_STADISCONNECTED");
static DEFINE_STRING_P(SOFTAPMODE_PROBEREQRECVED, "SOFTAPMODE_PROBEREQRECVED");
#endif


/*
 * Ensure supplied parameters are valid
 */
/*
bool checkWifiInfo(const wifi_info_t& info)
{
  #define MIN_HOSTNAME_LENGTH   5
  #define MAX_HOSTNAME_LENGTH   32

  if (info.hostname.length() < MIN_HOSTNAME_LENGTH || info.hostname.length() > MAX_HOSTNAME_LENGTH) {
    debug_w("Hostname '%s' length invalid", info.hostname.c_str());
    return false;
  }

  #define MIN_SSID_LENGTH       5
  #define MAX_SSID_LENGTH       31
  #define MAX_PASSWORD_LENGTH   63

  if (info.ssid.length() < MIN_SSID_LENGTH || info.ssid.length() > MAX_SSID_LENGTH) {
    debug_w("SSID '%s' length invalid", info.ssid.c_str());
    return false;
  }

  if (info.password.length() > MAX_PASSWORD_LENGTH) {
    debug_w("Password '%s' too long", info.password.c_str());
    return false;
  }

  return true;
}
*/

class CNetworkConfig : public CConfigFile
{
  public:
    CNetworkConfig()
    {
      init(FILE_NETWORK_CONFIG());
    }

    JsonObject& network()
    {
      return childObject(root(), CONFIG_NETWORK());
    }
};



static char hexChar(uint8_t c)
{
  if (c < 10)
    return '0' + c;
  return 'a' + c - 10;
}

String macToStr(uint8_t hwaddr[6])
{
  char buf[20];
  uint8_t n = 0;
  for (uint8_t i = 0; i < 6; ++i) {
    buf[n++] = hexChar(hwaddr[i] >> 4);
    buf[n++] = hexChar(hwaddr[i] & 0x0F);
    buf[n++] = ':';
  }
  buf[--n] = '\0';
  return String(buf);
}


String authModeToStr(AUTH_MODE mode)
{
  static const char PROGMEM tags[] =
    "OPEN\0"
    "WEP\0"
    "WPA\0"
    "WPA2\0"
    "WPA/WPA2\0";
  return String_P(tags, sizeof(tags)).szGetText(mode);
}


static DEFINE_STRING_P(STR_REASON, "REASON_");

/*
 * Disconnect message
 */
static String reasonToStr(uint8_t reason)
{
#define XX(_tag) \
  case REASON_ ## _tag: \
    return String_P(PSTR(#_tag));

  switch(reason) {
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
void CNetworkManager::startMDNS()
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
    .txt_data = { (char*)MDNS_VERSION }
  };
  espconn_mdns_init(&info);
#endif
}


void CNetworkManager::onStatusChange(network_callback_t callback)
{
  m_onStatusChange = callback;
}


void CNetworkManager::wifiEventHandler(System_Event_t *evt)
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
bool CNetworkManager::accessPointMode(bool enable)
{
  if (m_dnsServer) {
    delete m_dnsServer;
    m_dnsServer = nullptr;
  }

  WifiStation.enable(true);

  bool ret = true;
  if (enable) {
    wifi_info_t info;
    {
      CNetworkConfig config;
      JsonObject& ap = config.network()[CONFIG_AP()];
      info.ssid = ap[ATTR_SSID()].asString() ?: DEFAULT_AP_SSID();
      info.password = ap[ATTR_PASSWORD()].asString() ?: DEFAULT_AP_PASSWORD();
    }

    WifiStation.disconnect();
    WifiAccessPoint.enable(true);
    ret = WifiAccessPoint.config(info.ssid, info.password, info.password.length() ? AUTH_WPA2_PSK : AUTH_OPEN);

    WifiAccessPoint.setIP(g_apIP);
    m_dnsServer = new DNSServer();
    if (m_dnsServer)
      m_dnsServer->start(DNS_PORT, "*", g_apIP);

    debug_i("AP mode, SSID '%s' - %s", WifiAccessPoint.getSSID().c_str(), ret ? "OK" : "FAILED");
  }
  else {
    WifiAccessPoint.enable(false);
    WifiStation.connect();
    debug_i("AP mode disabled");
  }

  statusChanged(WifiAccessPoint.isEnabled() ? nwc_apEnabled : nwc_apDisabled);
  return ret;
}


void CNetworkManager::configure(command_connection_t connection, JsonObject& json)
{
  if (!m_configConnection->isValid())
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
      CNetworkConfig config;
      config.network()[ATTR_HOSTNAME()] = m_hostname;
      config.save();
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

  deferCallback(callback, reinterpret_cast<os_param_t>(info));

  setPending(json);
}


/*
 * Notify client of result of configuration.
 *
 * @param errReason System-defined error code, 0 on success.
 */
void CNetworkManager::configComplete(uint8_t errReason)
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
    deferCallback([](os_param_t) {
      networkManager.accessPointMode(false);
    });
}


void CNetworkManager::scanComplete(void* arg, STATUS status)
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

    CBssInfo bss;
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


void CNetworkManager::scan(command_connection_t connection, JsonObject& json)
{
  // Scan in progress ?
  if (m_scanConnection) {
    setError(json);
    return;
  }

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


void CNetworkManager::begin()
{
  wifi_set_event_handler_cb([](System_Event_t* evt) {
     networkManager.wifiEventHandler(evt);
  });

  WifiAccessPoint.enable(false);
  WifiStation.enable(true);

  {
    CNetworkConfig config;
    JsonObject& network = config.network();
    m_hostname = network[ATTR_HOSTNAME()].asString() ?: DEFAULT_HOSTNAME();
    m_serverPort = network[ATTR_SERVER_PORT()].as<uint16_t>() ?: DEFAULT_SERVER_PORT;
  }

  WifiStation.setHostname(m_hostname);
}


String CNetworkManager::getMethod() const
{
  return METHOD_NETWORK();
}


void CNetworkManager::handleMessage(command_connection_t connection, JsonObject& json)
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



void CNetworkManager::ntpInit()
{
  m_ntpClient.setNtpServer("pool.ntp.org");
  m_ntpClient.setAutoQuery(true);
  m_ntpClient.requestTime();
}


void CNetworkManager::staticOnNtpReceive(NtpClient& client, time_t timestamp)
{
  debug_i("%s(%u)", __FUNCTION__, timestamp);

  // Get timezone
  // United Kingdom (London, Belfast)
  timechange_rule_t BST = { Last, Sun, Mar, 1, 60 };
  timechange_rule_t GMT = { Last, Sun, Oct, 2,  0 };
  CDaylight tz(BST, GMT);

  time_t local = tz.toLocal(timestamp);
  debug_i("Local = %d", local);
  time_t now = SystemClock.now(eTZ_Local);
  debug_i("System = %d", now);

  debug_i("Local time: %s", DateTime(local).toFullDateTimeString().c_str());

  // If time hasn't changed, don't need to update anything else
  if (abs(now - local) < 2) {
    debug_i("Time unchanged");
    return;
  }

  float diff = (local - timestamp) / SECS_PER_HOUR;
  debug_i("TZ diff = %f", diff);
  SystemClock.setTimeZone(diff);
  SystemClock.setTime(local, eTZ_Local);
  debug_i("SystemClock: UTC = %s", SystemClock.getSystemTimeString(eTZ_UTC).c_str());
  debug_i("SystemClock: LOC = %s", SystemClock.getSystemTimeString(eTZ_Local).c_str());

  // Location co-ordinates should be part of config
  const float lat = 52.067; // 52.01486;
  const float lng = -0.7867; // -0.70126;
  const float tz_offset = 0;
  //

  CSolarCalculator mk(lat, lng, tz_offset);

  bool dst = tz.utcIsDST(timestamp);
  DateTime dt(local);
  int sunrise = mk.sunrise(dt.Year, dt.Month + 1, dt.Day, dst);
  debug_i("Sunrise: %02u:%02u", sunrise / 60, sunrise % 60);

  int sunset = mk.sunset(dt.Year, dt.Month + 1, dt.Day, dst);
  debug_i("Sunset: %02u:%02u", sunset / 60, sunset % 60);

  networkManager.statusChanged(nwc_timeUpdated);
}


