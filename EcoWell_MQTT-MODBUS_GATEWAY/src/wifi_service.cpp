#include "wifi_service.h"
#include "logger.h"
#include "system_events.h"
#include "event_service.h"
#include <esp_err.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

#define MODULE "WIFI"

typedef enum {
  WIFI_EVT_START = 100,
  WIFI_EVT_STA_CONNECTED = 101,
  WIFI_EVT_GOT_IP = 102,
  WIFI_EVT_LOST_IP = 103,
  WIFI_EVT_DISCONNECTED = 104,
  WIFI_EVT_RETRY_TIMEOUT = 105
} connection_event_t;

typedef struct {
  connection_event_t event;
  WiFiEventInfo_t info;
} wifi_msg_t;

static app_wifi_config_t gConfig;

static QueueHandle_t xWifiQueueHandle = NULL;
static TaskHandle_t xWifiTaskHandle = NULL;
static TimerHandle_t xStaReconnectTimer = NULL;

static connection_state_t wConnectionState = WIFI_CONNECTION_STATE_IDLE;
static wifi_state_t gWifiState = WIFI_STATE_DEINITIALIZED;

// Spinlock to protect cross-task queries (e.g., from system_service)
static portMUX_TYPE gWifiStateMux = portMUX_INITIALIZER_UNLOCKED;

// --- Thread Safe State Management ---
connection_state_t wifi_get_connection_state(void){
  connection_state_t stateCopy;
  portENTER_CRITICAL(&gWifiStateMux);
  stateCopy = wConnectionState;
  portEXIT_CRITICAL(&gWifiStateMux);
  return stateCopy;
}

static void wifi_set_connection_state(connection_state_t newState){
  portENTER_CRITICAL(&gWifiStateMux);
  wConnectionState = newState;
  portEXIT_CRITICAL(&gWifiStateMux);
}


void vStaReconnectTimerCB(TimerHandle_t pxTimer){
  wifi_msg_t wifi_reconnect_msg = {};
  wifi_reconnect_msg.event = WIFI_EVT_RETRY_TIMEOUT;

  if(xQueueSend(xWifiQueueHandle, &wifi_reconnect_msg, 0) != pdPASS ){
    LOG_ERROR(MODULE ,"message post into queue fail");
    return;
  }
}

bool wifi_is_connected(){
  return WiFi.isConnected();
}

void wifiFSTMTask(void *pv){
  wifi_msg_t rx_wifi_msg_buff;
  while(true){
    if(xQueueReceive(xWifiQueueHandle, &rx_wifi_msg_buff, portMAX_DELAY) != pdPASS ){
      LOG_ERROR(MODULE, "queue message receive fail");
      continue;
    }
    
    switch(wifi_get_connection_state()){ // Thread-safe read
      case(WIFI_CONNECTION_STATE_IDLE):
        if(rx_wifi_msg_buff.event == WIFI_EVT_START){
          LOG_INFO(MODULE, "Connecting...");
          WiFi.begin(gConfig.ssid, gConfig.password);
          wifi_set_connection_state(WIFI_CONNECTION_STATE_CONNECTING);
        }
        else{
          LOG_ERROR(MODULE, "Unhandled Event");
        }
        break;
      case(WIFI_CONNECTION_STATE_CONNECTING):
        if(rx_wifi_msg_buff.event == WIFI_EVT_STA_CONNECTED){
          LOG_INFO(MODULE ,"STA connected to AP: %s on channel: %d", WiFi.SSID().c_str(), WiFi.channel());
          wifi_set_connection_state(WIFI_CONNECTION_STATE_CONNECTED);
        }
        else if(rx_wifi_msg_buff.event == WIFI_EVT_DISCONNECTED){
          LOG_WARN(MODULE, "STA disconnected from AP, Reason: %s", WiFi.disconnectReasonName((wifi_err_reason_t)rx_wifi_msg_buff.info.wifi_sta_disconnected.reason));
          if(gConfig.autoReconnect){
            if(xTimerReset(xStaReconnectTimer, 0) != pdPASS){
              LOG_ERROR(MODULE, "STA reconnect start fail");
              break;
            }
            LOG_INFO(MODULE ,"STA reconnect start success, %d sec", gConfig.reconnectTimeoutMs/1000);
            wifi_set_connection_state(WIFI_CONNECTION_STATE_RETRY_WAIT);
          }
        }
        else{
          LOG_ERROR(MODULE, "Unhandled Event");
        }
        break;
      case(WIFI_CONNECTION_STATE_CONNECTED):
        if(rx_wifi_msg_buff.event == WIFI_EVT_DISCONNECTED){
          LOG_WARN(MODULE, "STA disconnected from AP, Reason: %s", WiFi.disconnectReasonName((wifi_err_reason_t)rx_wifi_msg_buff.info.wifi_sta_disconnected.reason));
          if(gConfig.autoReconnect){
            if(xTimerReset(xStaReconnectTimer, 0) != pdPASS){
              LOG_ERROR(MODULE, "STA reconnect start fail");
              break;
            }
            LOG_INFO(MODULE ,"STA reconnect start success, %d sec", gConfig.reconnectTimeoutMs/1000);
            wifi_set_connection_state(WIFI_CONNECTION_STATE_RETRY_WAIT);
          }
        }
        else if(rx_wifi_msg_buff.event == WIFI_EVT_GOT_IP){
          event_post(APP_EVENTS, APP_EVENT_NETWORK_UP, NULL, 0);
          LOG_INFO(MODULE ,"STA got IP: %s", WiFi.localIP().toString().c_str());
          LOG_INFO(MODULE ,"STA got Gateway: %s", WiFi.gatewayIP().toString().c_str());
          LOG_INFO(MODULE ,"STA got DNS: %s", WiFi.dnsIP().toString().c_str());
          wifi_set_connection_state(WIFI_CONNECTION_STATE_GOT_IP);
        }
        else{
          LOG_ERROR(MODULE, "Unhandled Event");
        }
        break;
      case(WIFI_CONNECTION_STATE_RETRY_WAIT):
        if(rx_wifi_msg_buff.event == WIFI_EVT_STA_CONNECTED){
          if(xTimerIsTimerActive(xStaReconnectTimer) != pdFALSE){
            if(xTimerStop(xStaReconnectTimer, 0) != pdPASS){
              LOG_ERROR(MODULE ,"STA reconnect stop fail");
              break;
            }
          }
          wifi_set_connection_state(WIFI_CONNECTION_STATE_CONNECTED);
        }
        else if(rx_wifi_msg_buff.event == WIFI_EVT_RETRY_TIMEOUT){
          if(wifi_is_connected()){
            LOG_INFO(MODULE ,"Already connected, ignore reconnect timeout");
            wifi_set_connection_state(WIFI_CONNECTION_STATE_CONNECTED);
            break;
          }
          LOG_INFO(MODULE ,"Reconnecting...");
          WiFi.begin(gConfig.ssid, gConfig.password);
          wifi_set_connection_state(WIFI_CONNECTION_STATE_CONNECTING);
        }
        else if(rx_wifi_msg_buff.event == WIFI_EVT_DISCONNECTED){
          if(xTimerReset(xStaReconnectTimer, 0) != pdPASS){
            LOG_ERROR(MODULE, "STA reconnect start fail");
            break;
          }
          LOG_INFO(MODULE ,"STA reconnect start success, %d sec", gConfig.reconnectTimeoutMs/1000);
          wifi_set_connection_state(WIFI_CONNECTION_STATE_RETRY_WAIT);
        }
        else if(rx_wifi_msg_buff.event == WIFI_EVT_LOST_IP){
          LOG_WARN(MODULE, "STA lost IP address");
          wifi_set_connection_state(WIFI_CONNECTION_STATE_RETRY_WAIT);
        }
        else{
          LOG_ERROR(MODULE, "Unhandled Event");
        }
        break;
      case(WIFI_CONNECTION_STATE_GOT_IP):
        if(rx_wifi_msg_buff.event == WIFI_EVT_LOST_IP){
          event_post(APP_EVENTS, APP_EVENT_NETWORK_DOWN, NULL, 0);
          LOG_WARN(MODULE, "STA lost IP address");
          wifi_set_connection_state(WIFI_CONNECTION_STATE_CONNECTED);
        }
        else if(rx_wifi_msg_buff.event == WIFI_EVT_DISCONNECTED){
          event_post(APP_EVENTS, APP_EVENT_NETWORK_DOWN, NULL, 0);
          LOG_WARN(MODULE ,"STA disconnected from AP, Reason: %s", WiFi.disconnectReasonName((wifi_err_reason_t)rx_wifi_msg_buff.info.wifi_sta_disconnected.reason));
          if(gConfig.autoReconnect){
            if(xTimerReset(xStaReconnectTimer, 0) != pdPASS){
              LOG_ERROR(MODULE, "STA reconnect start fail");
              break;
            }
            LOG_INFO(MODULE ,"STA reconnect start success, %d sec", gConfig.reconnectTimeoutMs/1000);
            wifi_set_connection_state(WIFI_CONNECTION_STATE_RETRY_WAIT);
          }
        }
        else{
          LOG_ERROR(MODULE, "Unhandled Event");
        }
        break;
      default: LOG_ERROR(MODULE, "Unhandled state");
    }
  }
}

void wifiEventHandler(WiFiEvent_t event, WiFiEventInfo_t info){
  wifi_msg_t tx_wifi_msg_buff = {};
  switch(event){
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      tx_wifi_msg_buff.event = WIFI_EVT_STA_CONNECTED;
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      tx_wifi_msg_buff.event = WIFI_EVT_GOT_IP;
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      tx_wifi_msg_buff.event = WIFI_EVT_DISCONNECTED;
      tx_wifi_msg_buff.info = info;
      break;
    case ARDUINO_EVENT_WIFI_STA_LOST_IP:
      tx_wifi_msg_buff.event = WIFI_EVT_LOST_IP;
      break;
    default:
        return;
  }

  if(xQueueSend(xWifiQueueHandle, &tx_wifi_msg_buff, 0) != pdPASS ){
    LOG_ERROR(MODULE, "message post into queue fail");
    return;
  }
}

sys_status_t wifi_init(const app_wifi_config_t* cfg){
  if(gWifiState != WIFI_STATE_DEINITIALIZED){
    return SYS_ERR_INVALID_STATE;
  }

  if(cfg == NULL){
    LOG_ERROR(MODULE, "Invalid params");
    return SYS_ERR_INVALID_PARAM;
  }

  LOG_INFO(MODULE ,"Initializing...");
  memcpy(&gConfig, cfg, sizeof(app_wifi_config_t));

  xStaReconnectTimer = xTimerCreate("STA_RECONNECT_TIMER", pdMS_TO_TICKS(gConfig.reconnectTimeoutMs), pdFALSE, NULL, vStaReconnectTimerCB);
  if(xStaReconnectTimer == NULL){
    LOG_ERROR(MODULE, "create STA reconnect timer fail");
    return SYS_ERR_NO_MEMORY;
  }

  xWifiQueueHandle = xQueueCreate(30, sizeof(wifi_msg_t));
  if(xWifiQueueHandle == NULL){
    LOG_ERROR(MODULE, "create queue fail");
    return SYS_ERR_NO_MEMORY;
  }

  WiFi.mode(gConfig.mode);
  WiFi.persistent(false);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(false);
  WiFi.onEvent(wifiEventHandler);
  
  gWifiState = WIFI_STATE_INITIALIZED;
  LOG_INFO(MODULE ,"Initialize success");
  return SYS_OK;
}

sys_status_t wifi_start(){
  if(gWifiState != WIFI_STATE_INITIALIZED && gWifiState != WIFI_STATE_STOPPED){
    return SYS_ERR_INVALID_STATE;
  }
  LOG_INFO(MODULE ,"Starting...");
  
  if(xTaskCreatePinnedToCore(wifiFSTMTask, "wifi", 4096, NULL, 5, &xWifiTaskHandle, 1) != pdPASS){
    LOG_ERROR(MODULE, "Error create app wifi task");
    return SYS_ERR_NO_MEMORY;
  }

  wifi_msg_t wifi_start_msg = {};
  wifi_start_msg.event = WIFI_EVT_START;

  if(xQueueSend(xWifiQueueHandle, &wifi_start_msg, 0) != pdPASS ){
    LOG_ERROR(MODULE, "message post into queue fail");
    return SYS_ERR_FAIL;
  }
  
  gWifiState = WIFI_STATE_RUNNING;
  return SYS_OK;
}

sys_status_t wifi_stop(void){
  if(gWifiState != WIFI_STATE_RUNNING){
    return SYS_ERR_INVALID_STATE;
  }

  if(xWifiTaskHandle != NULL){
    vTaskDelete(xWifiTaskHandle);
    xWifiTaskHandle = NULL;
  }

  if(xTimerStop(xStaReconnectTimer, 0) != pdPASS){
    LOG_ERROR(MODULE ,"STA reconnect stop fail");
    return SYS_ERR_FAIL;
  }
  
  WiFi.disconnect(true);
  wifi_set_connection_state(WIFI_CONNECTION_STATE_IDLE);
  gWifiState = WIFI_STATE_STOPPED;
  return SYS_OK;
}