#include <PsychicMqttClient.h>
#include "mqtt_service.h"
#include "subscriber.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "logger.h"
#include "system_events.h"
#include "event_service.h"

#define MODULE "MQTT-CLIENT"
#define MAX_URI_LENGTH 64

static QueueHandle_t gTxQueue = NULL;
static TaskHandle_t gTaskHandle = NULL;

static mqtt_config_t gConfig;
static mqtt_state_t gState = MQTT_STATE_DEINITIALIZED;

static portMUX_TYPE gStateMux = portMUX_INITIALIZER_UNLOCKED; 

static PsychicMqttClient gClient;
static char gURI[MAX_URI_LENGTH];

// Thread-safe state getter
mqtt_state_t mqtt_get_state(){
  mqtt_state_t stateCopy;
  portENTER_CRITICAL(&gStateMux);
  stateCopy = gState;
  portEXIT_CRITICAL(&gStateMux);
  return stateCopy;
}

// Thread-safe state setter
static void mqtt_set_state(mqtt_state_t newState){
  portENTER_CRITICAL(&gStateMux);
  gState = newState;
  portEXIT_CRITICAL(&gStateMux);
}

// QueueHandle_t mqtt_get_tx_queue(){
//   if(mqtt_get_state() == MQTT_STATE_DEINITIALIZED) return NULL;
//   return gTxQueue;
// }

sys_status_t mqtt_subscribe(const char *topic, uint8_t qos){
  if(mqtt_get_state() != MQTT_STATE_RUNNING){
    LOG_ERROR(MODULE, "Invalid state");
    return SYS_ERR_INVALID_STATE;
  }

  if(topic == NULL){
    LOG_ERROR(MODULE, "Invalid params");
    return SYS_ERR_INVALID_PARAM;
  }
      
  int msgId = gClient.subscribe(topic, qos);
  if(msgId == -1){
    LOG_ERROR(MODULE, "Subscribe failed, topic: %s", topic);
    return SYS_ERR_FAIL;
  }

  LOG_INFO(MODULE, "Subscribing at QoS: %d, packet id: %d, topic: %s", qos, msgId, topic);
  return SYS_OK;
}

sys_status_t mqtt_publish(const mqtt_msg_t *msg){
  if(mqtt_get_state() != MQTT_STATE_RUNNING){
    LOG_ERROR(MODULE, "Invalid state");
    return SYS_ERR_INVALID_STATE;
  } 

  if(msg == NULL){
    LOG_ERROR(MODULE, "Invalid params");
    return SYS_ERR_INVALID_PARAM;
  }
  
  if(xQueueSend(gTxQueue, msg, 500) != pdPASS){
    LOG_ERROR(MODULE, "Failed to post message");
    return SYS_ERR_FAIL;
  } 
  return SYS_OK;
}

static void mqttTask(void *pv){
  mqtt_msg_t msg;
  LOG_INFO(MODULE, "Task started");
  
  while (true){
    if(xQueueReceive(gTxQueue, &msg, portMAX_DELAY) != pdPASS) continue;
    
    if(mqtt_get_state() != MQTT_STATE_RUNNING){
      LOG_WARN(MODULE, "Not connected to the broker, dropping message");
      continue;
    }

    int msgId = gClient.publish(msg.topic, msg.qos, msg.retain, msg.payload, msg.len);
    if(msgId == -1){
      LOG_WARN(MODULE, "Publish failed, topic: %s", msg.topic);
      continue;
    }
    LOG_INFO(MODULE, "Publishing at QoS %d, packetId: %d, topic: %s, payload: %s", msg.qos, msgId, msg.topic, msg.payload);
  }
}

sys_status_t mqtt_init(const mqtt_config_t* cfg){
  if(mqtt_get_state() != MQTT_STATE_DEINITIALIZED){
    LOG_ERROR(MODULE, "invalid state");
    return SYS_ERR_INVALID_STATE;
  } 

  if(cfg == NULL){
    LOG_ERROR(MODULE, "invalid parmas");
    return SYS_ERR_INVALID_PARAM;
  }

  LOG_INFO(MODULE, "Initializing...");
  memcpy(&gConfig, cfg, sizeof(mqtt_config_t));
  snprintf(gURI, sizeof(gURI), "%s://%s:%u", gConfig.secure ? "mqtts" : "mqtt", gConfig.broker, gConfig.port);

  gTxQueue = xQueueCreate(30, sizeof(mqtt_msg_t));
  if(gTxQueue == NULL){
    LOG_ERROR(MODULE, "Failed to create queue");
    return SYS_ERR_NO_MEMORY;
  }

  gClient
  .setServer(gURI)
  .setCredentials(gConfig.username, gConfig.password)
  .setClientId(gConfig.clientId)
  .setKeepAlive(gConfig.keepAlive)
  .setCleanSession(gConfig.cleanSession)
  .setAutoReconnect(gConfig.autoReconnect)
  .setCACert(gConfig.caCert)
  .setWill("ECO/WS-1/MQTT-STATUS", 1, true, "{\"value\":\"offline\"}")
  .onConnect([](bool sp){
    LOG_INFO(MODULE, "Connected to the broker");
    event_post(APP_EVENTS, APP_EVENT_MQTT_CONNECTED, NULL, 0);
    mqtt_set_state(MQTT_STATE_RUNNING);
    gClient.publish("ECO/WS-1/STATUS", 1, true, "{\"value\":\"online\"}");
  })
  .onDisconnect([](bool sp){
    LOG_INFO(MODULE, "Disconnected from broker, Auto-reconnecting...");
    event_post(APP_EVENTS, APP_EVENT_MQTT_DISCONNECTED, NULL, 0);
    if(mqtt_get_state() != MQTT_STATE_STOPPED) mqtt_set_state(MQTT_STATE_CONNECTING);
  })
  .onPublish([](uint16_t packetId){
    LOG_INFO(MODULE, "Publish acknowledged, packet id: %d", packetId);
  })
  .onSubscribe([](uint16_t packetId){
    LOG_INFO(MODULE, "Subscribe acknowledged, packet id: %d", packetId);
  })
  .onMessage([](char* topic, char* payload, int qos, bool retain, bool dup){
    mqtt_msg_t msg = {};

    strncpy(msg.topic, topic, sizeof(msg.topic)-1);
    msg.topic[sizeof(msg.topic)-1] = '\0';
    strncpy(msg.payload, payload, sizeof(msg.payload)-1);
    msg.payload[sizeof(msg.payload)-1] = '\0';
    msg.len = strnlen(payload, sizeof(msg.payload)-1);
    msg.qos = qos;
    msg.retain = retain;

    subscriber_post(&msg);
  });

  mqtt_set_state(MQTT_STATE_INITIALIZED);
  LOG_INFO(MODULE, "Initialize success");
  return SYS_OK;
}

sys_status_t mqtt_start(void){
  mqtt_state_t current = mqtt_get_state();
  if(current != MQTT_STATE_INITIALIZED && current != MQTT_STATE_STOPPED){
    LOG_ERROR(MODULE, "invalid state");
    return SYS_ERR_INVALID_STATE;
  }
  
  LOG_INFO(MODULE, "Starting...");
  
  if(xTaskCreatePinnedToCore(mqttTask, "mqtt_task", 4096, NULL, 5, &gTaskHandle, 1) != pdPASS){
    LOG_ERROR(MODULE, "Failed to create task");
    return SYS_ERR_NO_MEMORY;
  }

  gClient.connect();
  mqtt_set_state(MQTT_STATE_CONNECTING);
  return SYS_OK;
}

sys_status_t mqtt_stop(void){
  mqtt_state_t current = mqtt_get_state();
  if(current != MQTT_STATE_RUNNING && current != MQTT_STATE_CONNECTING){
    LOG_ERROR(MODULE, "invalid state");
    return SYS_ERR_INVALID_STATE;
  }
  
  LOG_INFO(MODULE, "Stopping...");
  if(gTaskHandle != NULL){
    vTaskDelete(gTaskHandle);
    gTaskHandle = NULL;
  }

  mqtt_set_state(MQTT_STATE_STOPPED);
  gClient.disconnect();

  LOG_INFO(MODULE, "Stopped");
  return SYS_OK;
}