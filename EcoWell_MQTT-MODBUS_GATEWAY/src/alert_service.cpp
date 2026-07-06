#include "alert_service.h"
#include "logger.h"
#include "event_service.h"
#include "system_events.h"
#include "mqtt_service.h"
#include <ArduinoJson.h>
#include <string.h>

#define MODULE "ALERT-SVC"

typedef enum {
  ALERT_SEVERITY_INFO = 800,
  ALERT_SEVERITY_WARNING = 801,
  ALERT_SEVERITY_CRITICAL = 802
} alert_severity_t;

typedef struct {
  int32_t eventId;
  alert_severity_t severity;
  char message[64];
  uint32_t timestamp;
} alert_msg_t;

static QueueHandle_t gAlertQueue = NULL;
static TaskHandle_t gAlertTaskHandle = NULL;
static bool gInitialized = false;
static bool gRunning = false;

// Helper to convert severity enum to string
static const char* severity_to_str(alert_severity_t severity) {
  switch (severity) {
    case ALERT_SEVERITY_INFO:     return "INFO";
    case ALERT_SEVERITY_WARNING:  return "WARNING";
    case ALERT_SEVERITY_CRITICAL: return "CRITICAL";
    default:                      return "UNKNOWN";
  }
}

// Maps incoming Event Loop events to human-readable Alert Messages
static bool event_to_alert(esp_event_base_t base, int32_t id, alert_severity_t* severity, char* message, size_t maxLen) {
  if (base == APP_EVENTS) {
    switch (id) {
      case APP_EVENT_SYSTEM_FAULT:
        *severity = ALERT_SEVERITY_CRITICAL;
        strncpy(message, "System: Critical internal software or configuration fault!", maxLen - 1);
        return true;

      default:
        return false;
    }
  } 
  else if (base == CORE_EVENTS) {
    switch (id) {
      case CORE_EVENT_FAULT_LOW_PRESSURE:
        *severity = ALERT_SEVERITY_CRITICAL;
        strncpy(message, "Emergency: Water pressure below threshold!", maxLen - 1);
        return true;

      case CORE_EVENT_FAULT_LOW_SALT:
        *severity = ALERT_SEVERITY_CRITICAL;
        strncpy(message, "Emergency: Salt level critically low!", maxLen - 1);
        return true;

      case CORE_EVENT_FAULT_DATA_LOSS:
        *severity = ALERT_SEVERITY_WARNING;
        strncpy(message, "Warning: Sensor communication lost!", maxLen - 1);
        return true;

      case CORE_EVENT_REGEN_STARTED:
        *severity = ALERT_SEVERITY_INFO;
        strncpy(message, "Status: Regeneration cycle started.", maxLen - 1);
        return true;

      case CORE_EVENT_REGEN_COMPLETED:
        *severity = ALERT_SEVERITY_INFO;
        strncpy(message, "Status: Regeneration cycle completed.", maxLen - 1);
        return true;

      case CORE_EVENT_REGEN_FAILED:
        *severity = ALERT_SEVERITY_CRITICAL;
        strncpy(message, "Emergency: Regeneration failed or timed out!", maxLen - 1);
        return true;

      default:
        return false;
    }
  }
  return false;
}

// Event Loop Handler (Runs inside the ESP Default Event Loop task context)
static void alert_event_handler(void* handler_args, esp_event_base_t event_base, int32_t event_id, void* event_data) {
  if (!gRunning || gAlertQueue == NULL) return;

  alert_msg_t alert = {};
  alert.eventId = event_id;
  alert.timestamp = millis();

  // Map the event. If the event is not alert-worthy, skip it.
  if (event_to_alert(event_base, event_id, &alert.severity, alert.message, sizeof(alert.message))) {
    // Send to Alert Task Queue immediately (0 block time)
    if (xQueueSend(gAlertQueue, &alert, 0) != pdPASS) {
      LOG_WARN(MODULE, "Alert queue full, dropping alert!");
    }
  }
}

// Alert Serial Logging and MQTT Publishing Task
static void alertTask(void* pvParameters) {
  LOG_INFO(MODULE, "Alert task started");
  alert_msg_t alert;
  mqtt_msg_t msg;

  while (true) {
    // Wait indefinitely for an alert to be queued
    if (xQueueReceive(gAlertQueue, &alert, portMAX_DELAY) == pdPASS) {
      const char* sevStr = severity_to_str(alert.severity);

      // 1. Log to local Serial Monitor with appropriate severity colors
      if (alert.severity == ALERT_SEVERITY_CRITICAL) {
        LOG_ERROR(MODULE, "[%s] %s", sevStr, alert.message);
      } 
      else if (alert.severity == ALERT_SEVERITY_WARNING) {
        LOG_WARN(MODULE, "[%s] %s", sevStr, alert.message);
      } 
      else {
        LOG_INFO(MODULE, "[%s] %s", sevStr, alert.message);
      }

      // 2. Format JSON and publish to MQTT
      StaticJsonDocument<256> doc;
      doc["type"] = "alert";
      doc["severity"] = sevStr;
      doc["message"] = alert.message;
      doc["ts"] = alert.timestamp;

      memset(&msg, 0, sizeof(msg));
      strncpy(msg.topic, "ECO/WS-1/ALERTS", sizeof(msg.topic) - 1);
      msg.topic[sizeof(msg.topic) - 1] = '\0';
      msg.qos = 1;
      msg.retain = true;

      msg.len = serializeJson(doc, msg.payload, sizeof(msg.payload));
      if (msg.len > 0) {
        // Dispatched directly to the thread-safe MQTT Service queue!
        if (mqtt_publish(&msg) != SYS_OK) {
          LOG_ERROR(MODULE, "Failed to publish alert to MQTT broker");
        }
      }
    }
  }
}

sys_status_t alert_init(void) {
  if (gInitialized) return SYS_ERR_INVALID_STATE;
  LOG_INFO(MODULE, "Initializing...");

  gAlertQueue = xQueueCreate(15, sizeof(alert_msg_t));
  if (gAlertQueue == NULL) {
    LOG_ERROR(MODULE, "Failed to create queue");
    return SYS_ERR_NO_MEMORY;
  }

  // Register for ALL events on both base loops
  if (event_register(APP_EVENTS, ESP_EVENT_ANY_ID, alert_event_handler, NULL) != SYS_OK ||
      event_register(CORE_EVENTS, ESP_EVENT_ANY_ID, alert_event_handler, NULL) != SYS_OK) {
    LOG_ERROR(MODULE, "Failed to register event handlers");
    vQueueDelete(gAlertQueue);
    gAlertQueue = NULL;
    return SYS_ERR_FAIL;
  }

  gInitialized = true;
  LOG_INFO(MODULE, "Initialize success");
  return SYS_OK;
}

sys_status_t alert_start(void) {
  if (!gInitialized || gRunning) return SYS_ERR_INVALID_STATE;
  LOG_INFO(MODULE, "Starting...");

  if (xTaskCreatePinnedToCore(alertTask, "AlertTask", 4096, NULL, 5, &gAlertTaskHandle, 1) != pdPASS) {
    LOG_ERROR(MODULE, "Failed to create task");
    return SYS_ERR_NO_MEMORY;
  }

  gRunning = true;
  LOG_INFO(MODULE, "Started successfully");
  return SYS_OK;
}

sys_status_t alert_stop(void) {
  if (!gRunning) return SYS_ERR_INVALID_STATE;
  LOG_INFO(MODULE, "Stopping...");

  if (gAlertTaskHandle != NULL) {
    vTaskDelete(gAlertTaskHandle);
    gAlertTaskHandle = NULL;
  }
  
  gRunning = false;
  LOG_INFO(MODULE, "Stopped");
  return SYS_OK;
}