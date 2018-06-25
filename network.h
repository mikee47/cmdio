/*
 * network.h
 *
 *  Created on: 28 May 2018
 *      Author: Mike
 */

#ifndef __NETWORK_H
#define __NETWORK_H

#include "cmdhandler.h"
#include <timemgmt.h>
#include <solarcalc.h>


DECLARE_STRING_P(ATTR_PASSWORD)


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
};

typedef void (*network_callback_t)(network_change_t nwc);

typedef void (*network_scan_complete_t)(JsonObject& json, void* param);


String authModeToStr(AUTH_MODE mode);

String macToStr(uint8_t hwaddr[6]);


/*
 * Class to efficiently access bss_info list elements.
 */
class CBssInfo
{
  private:
    bss_info* m_info = nullptr;

  public:
    bool init(bss_info* info)
    {
      m_info = info;
      return m_info != nullptr;
    }

    String SSID()
    {
      return String((char*)m_info->ssid, std::min(m_info->ssid_len, (uint8_t)sizeof(m_info->ssid)));
    }

    String BSSID()
    {
      return macToStr(m_info->bssid);
    }

    AUTH_MODE authmode()
    {
      return m_info->authmode;
    }

    String authModeStr()
    {
      return authModeToStr(authmode());
    }

    uint8_t channel()
    {
      return m_info->channel;
    }

    int8_t rssi()
    {
      return m_info->rssi;
    }

    bool hidden()
    {
      return m_info->is_hidden != 0;
    }

    bool simplePair()
    {
      return m_info->simple_pair != 0;
    }

    bool next()
    {
      if (m_info)
        m_info = m_info->next.stqe_next;
      return m_info != nullptr;
    }
};




class CNetworkManager: public CCommandHandler
{
  private:
    // Only need this in AP mode so create it dynamically
    DNSServer* m_dnsServer = nullptr;
    // Persistent data for MDNS - libraries don't reliably keep copies
    String m_hostname;
    //
    uint16_t m_serverPort = 80;
    // The client connection being used to reconfigure network
    command_connection_t m_configConnection = nullptr;
    //
    network_callback_t m_onStatusChange = nullptr;
    // Network scan
    command_connection_t m_scanConnection = nullptr;
    // For keeping system clock accurate
    NtpClient m_ntpClient;

  private:

    void startMDNS();
    void ntpInit();

    void onNtpReceive(NtpClient& client, time_t timestamp);

    void configComplete(uint8_t reason);

    void scanComplete(void* arg, STATUS status);
    bool stationMode(const wifi_info_t& info);
    void wifiEventHandler(System_Event_t* evt);

    void statusChanged(network_change_t nwc)
    {
      if (m_onStatusChange)
        m_onStatusChange(nwc);
    }

  public:
    CNetworkManager() : m_ntpClient(NtpTimeResultDelegate(&CNetworkManager::onNtpReceive, this))
    { }

    void begin();

    void onStatusChange(network_callback_t callback);

    void scan(command_connection_t connection, JsonObject& json);

    void configure(command_connection_t connection, JsonObject& json);

    bool accessPointMode(bool enable);

    uint16_t webServerPort() const
    {
      return WifiAccessPoint.isEnabled() ? 80 : m_serverPort;
    }

    /* CCommandHandler */

    String getMethod() const;

    access_type_t minAccess() const
    {
      return access_user;
    }

    void handleMessage(command_connection_t connection, JsonObject& json);

    // ITimeManager
    time_t decodeTime(String s);
};


extern CNetworkManager networkManager;



#endif // __NETWORK_H
