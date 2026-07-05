#include <ArduinoJson.h>
#include "subscriber.h"
#include "logger.h"
#include "protocol_dispatcher.h"
#include "core.h" 

#define MODULE "SUBSCRIBER"

static QueueHandle_t gQueue = NULL;
static TaskHandle_t gTaskHandle = NULL;

static subscriber_state_t gState = SUBSCRIBER_STATE_DEINITIALIZED;

subscriber_state_t subscriber_get_state(){
  return gState;
}

sys_status_t subscriber_post(const mqtt_msg_t *msg){
  if(msg == NULL){
    LOG_ERROR(MODULE, "Invalid parameter");
    return SYS_ERR_INVALID_PARAM;
  }

  if(gState != SUBSCRIBER_STATE_RUNNING){
    LOG_WARN(MODULE, "Subscriber not running");
    return SYS_ERR_INVALID_STATE;
  }

  if(xQueueSend(gQueue, msg, 500) != pdPASS){
    LOG_WARN(MODULE, "Failed to post the message");
    return SYS_ERR_FAIL;
  }

  return SYS_OK;
}

static void subscriberTask(void *pv){
  mqtt_msg_t msg;

  LOG_INFO(MODULE, "Task started");
  for(uint16_t i=0; i<tag_count(); i++){
    const tag_config_t *tag = tag_get_at(i);
    if(tag->topicType == TAG_TOPIC_TYPE_SUBSCRIBE){
      sys_status_t status = mqtt_subscribe(tag->topic, 1);
      if(status != SYS_OK) LOG_ERROR(MODULE, "Failed to subscribe %s", tag->topic);
    }
  }

  while(true){
    if(xQueueReceive(gQueue, &msg, portMAX_DELAY) != pdPASS){
      LOG_ERROR(MODULE, "Failed to receive the message");
      continue;
    }

    const tag_config_t *tag = tag_find_by_topic(msg.topic);
    protocol_request_t request = {};
    request.operation = OP_WRITE;

    if(tag == NULL){
      LOG_WARN(MODULE, "Unknown topic");
      continue;
    }

    if(tag->topicType != TAG_TOPIC_TYPE_SUBSCRIBE){
      LOG_WARN(MODULE, "Topic is not subscribable");
      continue;
    }

    if(tag->access != TAG_ACCESS_WRITE_ONLY){ 
      LOG_WARN(MODULE,"Tag is read only");
      continue;
    }

    StaticJsonDocument<128> doc;
    DeserializationError err = deserializeJson(doc, msg.payload);
    if(err){
      LOG_ERROR(MODULE, "Invalid JSON");
      continue;
    }

    if(!doc.containsKey("value")){
      LOG_ERROR(MODULE, "Missing key, value");
      continue;
    }

    if(strcmp(tag->name, "cmd_regen") == 0){
      if(doc["value"] == true){
        LOG_INFO(MODULE, "Cloud requested regeneration. Forwarding to Core Engine...");
        if(core_request_regen() != SYS_OK) {
           LOG_ERROR(MODULE, "Failed to forward request to Core!");
        }
      }
      else if (doc["value"] == false) {
        LOG_INFO(MODULE, "Cloud requested abort regeneration. Forwarding to Core Engine...");
        core_abort_regen();
      }
      continue;
    }

    // (If you add other random write tags in the future, they will fall through here)
    request.tag = tag;
    switch(tag->dataType){ // Note: changed from valueDataType to dataType
      case TAG_BOOL:
        request.value.bv = doc["value"];
        break;
      case TAG_UINT16:
        request.value.u16v = doc["value"];
        break;
      case TAG_INT16:
        request.value.i16v = doc["value"];
        break;
      case TAG_UINT32:
        request.value.u32v = doc["value"];
        break;
      case TAG_INT32:
        request.value.i32v = doc["value"];
        break;
      case TAG_FLOAT32:
        request.value.f32v = doc["value"];
        break;
      default: 
        LOG_ERROR(MODULE, "Unsupported datatype");
        continue;
    }

    sys_status_t status = protocol_dispatcher_post(&request);
    if(status != SYS_OK){
      LOG_ERROR(MODULE, "Dispatch failed");
      continue;
    }
    LOG_INFO(MODULE, "Command dispatched directly: %s", tag->name);
  }
}

sys_status_t subscriber_init(){
  if(gState != SUBSCRIBER_STATE_DEINITIALIZED){
    LOG_ERROR(MODULE, "Invalid state");
    return SYS_ERR_INVALID_STATE;
  }

  LOG_INFO(MODULE, "Initializing...");

  gQueue = xQueueCreate(30, sizeof(mqtt_msg_t));
  if(gQueue == NULL){
    LOG_ERROR(MODULE, "Queue creation failed");
    return SYS_ERR_NO_MEMORY;
  }

  gState = SUBSCRIBER_STATE_INITIALIZED;
  LOG_INFO(MODULE, "Initialize success");
  return SYS_OK;
}

sys_status_t subscriber_start(){
  if(gState != SUBSCRIBER_STATE_INITIALIZED && gState != SUBSCRIBER_STATE_STOPPED){
    LOG_ERROR(MODULE, "Invalid state");
    return SYS_ERR_INVALID_STATE;
  }

  LOG_INFO(MODULE, "Starting...");
  // Hardcoded 4096 / 5 instead of STACK_LARGE / PRIORITY_NORMAL if you prefer
  if(xTaskCreatePinnedToCore(subscriberTask, "subscriber", 4096, NULL, 5, &gTaskHandle, 1) != pdPASS){
    LOG_ERROR(MODULE, "Failed to create task");
    return SYS_ERR_NO_MEMORY;
  }

  gState = SUBSCRIBER_STATE_RUNNING;
  return SYS_OK;
}

sys_status_t subscriber_stop(){
  if(gState != SUBSCRIBER_STATE_RUNNING){
    LOG_ERROR(MODULE, "Invalid state");
    return SYS_ERR_INVALID_STATE;
  }

  LOG_INFO(MODULE, "Stopping...");
  if(gTaskHandle != NULL){
    vTaskDelete(gTaskHandle);
    gTaskHandle = NULL;
  }

  gState = SUBSCRIBER_STATE_STOPPED;
  LOG_INFO(MODULE, "Stopped");

  return SYS_OK;
}