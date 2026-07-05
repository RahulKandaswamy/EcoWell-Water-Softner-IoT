#include "publisher.h"
#include "core.h" // Needed to fetch the machine state
#include <string.h>
#include <ArduinoJson.h>
#include "tag_runtime.h"
#include "mqtt_service.h"
#include "logger.h"

#define MODULE "PUBLISHER"

static publisher_config_t gConfig;
static publisher_state_t gState = PUBLISHER_STATE_DEINITIALIZED;
static TaskHandle_t gTaskHandle = NULL;

sys_status_t build_message(const tag_runtime_t* tag, mqtt_msg_t* msg){
  if(tag == NULL || msg == NULL){
    LOG_ERROR(MODULE, "Invalid params");
    return SYS_ERR_INVALID_PARAM;
  }

  if(tag->config == NULL){
    LOG_ERROR(MODULE, "Invalid config");
    return SYS_ERR_INVALID_PARAM;
  }

  strncpy(msg->topic, tag->config->topic, sizeof(msg->topic)-1);
  msg->topic[sizeof(msg->topic)-1] = '\0';
  msg->qos = 1;
  msg->retain = false;

  StaticJsonDocument<256> doc;
  doc["id"] = tag->config->tagId;
  doc["ts"]  = tag->timestamp;
  doc["type"] = "telemetry";
  
  switch(tag->config->dataType){
    case TAG_BOOL:
      doc["value"] = tag->value.bv;
      break;

    case TAG_UINT16:
      doc["value"] = tag->value.u16v;
      break;

    case TAG_INT16:
      doc["value"] = tag->value.i16v;
      break;

    case TAG_UINT32:
      doc["value"] = tag->value.u32v;
      break;

    case TAG_INT32:
      doc["value"] = tag->value.i32v;
      break;

    case TAG_FLOAT32:
      doc["value"] = tag->value.f32v;
      break;

    case TAG_STRING:
      doc["value"] = tag->value.strv;
      break;

    default:
      return SYS_ERR_INVALID_PARAM;
  }

  msg->len = serializeJson(doc, msg->payload, sizeof(msg->payload));
  if(msg->len == 0){
    LOG_ERROR(MODULE, "Failed to serialize message");
    return SYS_ERR_FAIL;
  }

  return SYS_OK;
}

static void publisherTask(void *pv){
  LOG_INFO(MODULE, "Task started");
  tag_runtime_t localTag;
  mqtt_msg_t msg;

  while (true){
    for (uint16_t i = 0; i < tag_count(); i++){
      tag_runtime_t* runtimeTag = tag_runtime_get_at(i);
      if (runtimeTag == NULL) continue;
      if (!runtimeTag->valid) continue;
      if (runtimeTag->config->topicType != TAG_TOPIC_TYPE_PUBLISH) continue;
      
      memset(&msg, 0, sizeof(msg));
      memset(&localTag, 0, sizeof(localTag));

      if(tag_runtime_lock() != SYS_OK) continue;
      memcpy(&localTag, runtimeTag, sizeof(tag_runtime_t));
      tag_runtime_unlock();
      
      if(build_message(&localTag, &msg) != SYS_OK){
        LOG_ERROR(MODULE, "Failed to build message");
        continue;
      }

      if(mqtt_publish(&msg) != SYS_OK){
        LOG_ERROR(MODULE, "Failed to post mqtt message");
      }
    }

    vTaskDelay(pdMS_TO_TICKS(gConfig.publishIntervalMs));
  }
}

publisher_state_t publisher_get_state(void){
  return gState;
}

sys_status_t publisher_init(const publisher_config_t *cfg){
  if (gState != PUBLISHER_STATE_DEINITIALIZED){
    LOG_ERROR(MODULE, "Invalid state");
    return SYS_ERR_INVALID_STATE;
  }

  if (cfg == NULL){
    LOG_ERROR(MODULE, "Invalid params");
    return SYS_ERR_INVALID_PARAM;
  }

  memcpy(&gConfig, cfg, sizeof(gConfig));

  gState = PUBLISHER_STATE_INITIALIZED;
  LOG_INFO(MODULE, "Initialize success");
  return SYS_OK;
}

sys_status_t publisher_start(void){
  if (gState != PUBLISHER_STATE_INITIALIZED && gState != PUBLISHER_STATE_STOPPED){
    LOG_ERROR(MODULE, "Invalid state");
    return SYS_ERR_INVALID_STATE;
  }

  LOG_INFO(MODULE, "Starting...");
  if(xTaskCreatePinnedToCore(publisherTask, "publisher", 4096, NULL, 5, &gTaskHandle, 1) != pdPASS){
    LOG_ERROR(MODULE, "Task creation failed");
    return SYS_ERR_NO_MEMORY;
  }

  gState = PUBLISHER_STATE_RUNNING;
  return SYS_OK;
}

sys_status_t publisher_stop(void){
  if(gState != PUBLISHER_STATE_RUNNING){
    LOG_ERROR(MODULE, "Invalid state");
    return SYS_ERR_INVALID_STATE;
  }

  LOG_INFO(MODULE, "Stopping...");
  if(gTaskHandle != NULL){
    vTaskDelete(gTaskHandle);
    gTaskHandle = NULL;
  }

  gState = PUBLISHER_STATE_STOPPED;
  LOG_INFO(MODULE, "Publisher stopped");
  return SYS_OK;
}