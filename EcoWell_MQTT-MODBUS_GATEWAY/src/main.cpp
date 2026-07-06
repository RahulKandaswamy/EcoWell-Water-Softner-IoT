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
#include "alert_service.h"

#define MODULE "APP"

const mqtt_config_t mqttConfig = {
  .broker = "13.233.38.147", // domain name or IP address of the MQTT broker
  .port = 1883, // non-secure port for MQTT
  .username = "ecowell_controller", // username for MQTT authentication, mandatory
  .password = "pass@123", // password for MQTT authentication, mandatory
  .clientId = "cntr-01", // unique client ID for this device, mandatory
  .secure = false, // changes, not required
  .cleanSession = false, // changes, not required
  .autoReconnect = true, // changes, not required
  .keepAlive = 120, // changes, not required
};

static const app_wifi_config_t wifiConfig = {
  .ssid = "Connect@2.4ghz", // SSID for WiFi connection, mandatory
  .password = "Start@2.4ghz", // password for WiFi connection, mandatory
  .autoReconnect = true, // changes, not required
  .reconnectTimeoutMs = 20000, // changes, not required
  .mode = WIFI_STA // changes, not required
};

static const modbus_config_t modbusConfig = {
  .baudrate = 19200,  // use your modbus slave device baud rate
  .serialConfig = SERIAL_8N2, // use your modbus slave device serial configuration
  .txPin = 17, // use your modbus slave device TX pin
  .rxPin = 18, // use your modbus slave device RX pin
  .timeoutMs = 2000 // changes, not required
};

static const acquisition_config_t acqConfig = {
  .scanIntervalMs = 10000
};

static const publisher_config_t publisherConfig = {
  .publishIntervalMs = 20000,
};

/**
 * @brief Handles critical system-level initialization and execution errors.
 * 
 * If a critical memory allocation or configuration failure occurs, this handler
 * logs the fault, posts a system fault event, and restarts the ESP32 if recovery is impossible.
 * 
 * @param status The system error code indicating the failure.
 */
static void fault_handler(sys_status_t status){
  LOG_ERROR(MODULE, "SYSTEM FAULT ENCOUNTERED");

  switch(status){
    case SYS_ERR_NO_MEMORY:
      LOG_ERROR(MODULE, "Out of heap memory! Task/Queue creation failed.");
      LOG_ERROR(MODULE, "Action: Restarting ESP32 in 5 seconds to recover...");
      delay(5000);
      esp_restart(); // ONLY reboot on actual memory exhaustion
      break;

    case SYS_ERR_INVALID_STATE:
      LOG_ERROR(MODULE, "Module accessed in an invalid state (uninitialized).");
      LOG_ERROR(MODULE, "Action: Check your setup() sequence to ensure all modules init before starting.");
      event_post(APP_EVENTS, APP_EVENT_SYSTEM_FAULT, NULL, 0); 
      break;

    case SYS_ERR_INVALID_PARAM:
      LOG_ERROR(MODULE, "Invalid configuration parameters (Null pointer passed).");
      LOG_ERROR(MODULE, "Action: Inspect config structures (wifiConfig, mqttConfig, etc.) in app.cpp.");
      event_post(APP_EVENTS, APP_EVENT_SYSTEM_FAULT, NULL, 0);
      break;

    case SYS_ERR_FAIL:
      LOG_ERROR(MODULE, "General software execution failure.");
      event_post(APP_EVENTS, APP_EVENT_SYSTEM_FAULT, NULL, 0);
      break;
      
    default:
      LOG_ERROR(MODULE, "Unknown system error (%d).", status);
      break;
  }      
}

/**
 * @file main.cpp
 * @brief Application Entry Point & Lifecycle Mediator.
 * 
 * Configures system-wide parameters, initializes all software modules in 
 * strict dependency order, and orchestrates the service lifecycle using 
 * event-driven callbacks triggered by network and MQTT state changes.
 */
/**
 * @brief Main event handler coordinating the network and MQTT client state changes.
 * 
 * Decoupled event callbacks that manage the lifecycle of other services:
 * - APP_EVENT_NETWORK_UP: Starts the MQTT service once IP is acquired.
 * - APP_EVENT_NETWORK_DOWN: Stops the MQTT client when Wi-Fi connection drops.
 * - APP_EVENT_MQTT_CONNECTED: Starts publisher/subscriber tasks.
 * - APP_EVENT_MQTT_DISCONNECTED: Stops publisher/subscriber tasks.
 * 
 * @param eventHandlerArg User argument passed to the handler (unused).
 * @param eventBase The event loop base (e.g. APP_EVENTS).
 * @param eventId The specific event ID triggered.
 * @param eventData Pointer to optional event-specific payload data.
 */
 

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

    case APP_EVENT_MODBUS_NETWORK_UP:
      LOG_INFO(MODULE, "Modbus network up");
      break;

    case APP_EVENT_MODBUS_NETWORK_DOWN:
      LOG_WARN(MODULE, "Modbus network down");
      break;

    default:
      break;
  }
}

sys_status_t app_init(){
  sys_status_t status;
  
  status = logger_init();
  if(status != SYS_OK) return status;

  status = event_init();
  if(status != SYS_OK) return status;

  status = event_register(APP_EVENTS, ESP_EVENT_ANY_ID, app_event_handler, NULL);
  if(status != SYS_OK) return status;

  status = alert_init(); 
  if(status != SYS_OK) return status;

  status = tag_runtime_init();
  if(status != SYS_OK) return status;

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


sys_status_t app_start(){
  sys_status_t status;

  status = alert_start();
  if(status != SYS_OK) return status;
  
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