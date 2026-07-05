#include <Arduino.h>
#include "logger.h"
#include "tag_registry.h"
#include "tag_runtime.h"
#include "system_err.h"
#include "acquisition.h"
#include "wifi_service.h"
#include "mqtt_service.h"
#include "modbus_service.h"
#include "protocol_dispatcher.h"
#include "core.h"
#include "system_events.h"
#include "event_service.h"
#include "publisher.h"
#include "subscriber.h"

#define MODULE "APP"

const mqtt_config_t mqttConfig = {
  .broker = "www.xpredictautomation.com",
  .port = 1883,
  .username = "XP_DEMO_MASTER",
  .password = "xplbs@123",
  .clientId = "XP-01",
  .secure = false,
  .cleanSession = false,
  .autoReconnect = true,
  .keepAlive = 120,
};

static const app_wifi_config_t wifiConfig = {
  .ssid = "Connect@2.4ghz",
  .password = "Start@2.4ghz",
  .autoReconnect = true,
  .reconnectTimeoutMs = 20000,
  .mode = WIFI_STA
};

static const modbus_config_t modbusConfig = {
  .baudrate = 19200,
  .serialConfig = SERIAL_8N2,
  .txPin = 17,
  .rxPin = 18,
  .timeoutMs = 2000
};

static const acquisition_config_t acqConfig = {
  .scanIntervalMs = 10000
};

static const publisher_config_t publisherConfig = {
  .publishIntervalMs = 20000,
};

static void fault_handler(sys_status_t status){
  LOG_ERROR(MODULE, "error (%d)", status);
  switch(status){
    case SYS_ERR_NO_MEMORY:
      LOG_ERROR(MODULE, "No memory");
      break;
    case SYS_ERR_INVALID_STATE:
      LOG_ERROR(MODULE, "Invalid state");
      break;
    case SYS_ERR_FAIL:
      LOG_ERROR(MODULE, "Error / Fail");
      break;
    case SYS_ERR_INVALID_PARAM:
      LOG_ERROR(MODULE, "Invalid Parameter");
      break;
    default:
      LOG_ERROR(MODULE, "Unhandled error");
      break;
  }      
}

static void app_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data){
  switch(id){
    case APP_EVENT_NETWORK_UP:
      LOG_INFO(MODULE, "Network Up, starting mqtt");
      mqtt_start();
      break;
    case APP_EVENT_NETWORK_DOWN:
      LOG_INFO(MODULE, "Network Down, stopping mqtt");
      if(mqtt_get_state() != MQTT_STATE_STOPPED) mqtt_stop();
      break;
    case APP_EVENT_MQTT_CONNECTED:
      LOG_INFO(MODULE, "Connected to mqtt broker, starting publisher and subscriber");
      publisher_start();
      subscriber_start();
      break;
    case APP_EVENT_MQTT_DISCONNECTED:
      LOG_WARN(MODULE, "Disconnected from mqtt broker, stopping publisher and subscriber");
      if(publisher_get_state() != PUBLISHER_STATE_STOPPED) publisher_stop();
      if(subscriber_get_state() != SUBSCRIBER_STATE_STOPPED) subscriber_stop();
      break;
    default:
      LOG_ERROR(MODULE, "Unhandled event %d", id);
  }
}
// app event hanlder end

sys_status_t app_init(){
  sys_status_t status;
  
  // // 1. Logging and Events (Foundation)
  status = logger_init();
  if(status != SYS_OK) return status;

  status = event_init();
  if(status != SYS_OK) return status;

  status = event_register(APP_EVENTS, ESP_EVENT_ANY_ID, app_event_handler, NULL);
  if(status != SYS_OK) return status;

  // // 2. Data Structures
  status = tag_runtime_init();
  if(status != SYS_OK) return status;

  // // 3. Logic & Services
  status = core_init();
  if(status != SYS_OK) return status;

  status = modbus_init(&modbusConfig);
  if(status != SYS_OK) return status;

  status = protocol_dispatcher_init();
  if(status != SYS_OK) return status;

  status = acquisition_init(&acqConfig);
  if(status != SYS_OK) return status;

  status = publisher_init(&publisherConfig);
  if(status != SYS_OK) return status;
  
  status = subscriber_init();
  if(status != SYS_OK) return status;

  status = wifi_init(&wifiConfig);
  if(status != SYS_OK) return status;

  status = mqtt_init(&mqttConfig);
  if(status != SYS_OK) return status;
  
  return SYS_OK;
}
// app init end

sys_status_t app_start(){
  sys_status_t status;
  status = core_start();
  if(status != SYS_OK) return status;

  status = protocol_dispatcher_start();
  if(status != SYS_OK) return status;
  
  status = acquisition_start();
  if(status != SYS_OK) return status;

  status = wifi_start();
  if(status != SYS_OK) return status;

  return SYS_OK;
}

void setup(){
  sys_status_t status;

  status = app_init();
  if(status != SYS_OK) fault_handler(status);
  LOG_INFO(MODULE, "Initialization success!");

  status = app_start();
  if(status != SYS_OK) fault_handler(status);
  LOG_INFO(MODULE, "start success!");
}

void loop(){
  // Main loop left intentionally empty, FreeRTOS handles the tasks
}