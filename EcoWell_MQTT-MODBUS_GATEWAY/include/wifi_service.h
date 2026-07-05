#ifndef WIFI_SERVICE_H
#define WIFI_SERVICE_H

#include <WiFi.h>
#include "system_err.h"


typedef enum{
  WIFI_CONNECTION_STATE_IDLE = 150,
  WIFI_CONNECTION_STATE_CONNECTING = 151,
  WIFI_CONNECTION_STATE_CONNECTED = 152, 
  WIFI_CONNECTION_STATE_RETRY_WAIT = 153,
  WIFI_CONNECTION_STATE_GOT_IP = 154
} connection_state_t;

typedef enum{
  WIFI_STATE_DEINITIALIZED = 200,
  WIFI_STATE_INITIALIZED = 201,
  WIFI_STATE_RUNNING = 202,
  WIFI_STATE_STOPPED = 203
} wifi_state_t;

typedef struct{
  const char *ssid;
  const char *password;
  bool autoReconnect;
  uint32_t reconnectTimeoutMs;
  wifi_mode_t mode;
} app_wifi_config_t;

sys_status_t wifi_init(const app_wifi_config_t* cfg);
sys_status_t wifi_start(void);
sys_status_t wifi_stop(void);
bool wifi_is_connected(void);
connection_state_t wifi_get_connection_state(void);

#endif