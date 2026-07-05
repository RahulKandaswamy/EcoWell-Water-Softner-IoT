#include "acquisition.h"
#include "logger.h"
#include "tag_registry.h"
#include "tag_runtime.h"


#define MODULE "ACQUISITION"

static TaskHandle_t gAcqTaskHandle = NULL;
static QueueHandle_t gAcqResponseQueue = NULL;
static acquisition_config_t gConfig;

static uint32_t gAcqScanInterval = 1000;
static acq_state_t gAcqState = ACQ_STATE_DEINITIALIZED;

// Called by Modbus (or other protocols) to deliver the raw result
sys_status_t acquisition_post_response(const protocol_response_t* response){
  if(response == NULL) return SYS_ERR_INVALID_PARAM;
  if(gAcqState != ACQ_STATE_RUNNING) return SYS_ERR_INVALID_STATE;
  if(gAcqResponseQueue == NULL) return SYS_ERR_INVALID_STATE;

  // Push to queue immediately (0 wait time) so the protocol callback is never blocked
  if(xQueueSend(gAcqResponseQueue, response, 0) != pdPASS){
    LOG_WARN(MODULE, "Response queue full!");
    return SYS_ERR_BUSY;
  }
  return SYS_OK;
}

static void acquisition_scan(void){
  for(uint16_t i = 0; i < tag_count(); i++){
    const tag_config_t* tag = tag_get_at(i);

    if(tag == NULL) continue;
    if(tag->access == TAG_ACCESS_WRITE_ONLY) continue;

    // Build the generic request
    protocol_request_t request;
    request.tag = tag;
    request.operation = OP_READ;

    sys_status_t status = protocol_dispatcher_post(&request);
    if(status != SYS_OK){
      LOG_ERROR(MODULE, "Failed to dispatch read request for tag %s", tag->name);
    }
  }
}

static void acquisition_task(void* pvParameters){
  LOG_INFO(MODULE, "Task started");
  uint32_t lastScanTime = 0;
  protocol_response_t response;

  while(true){
    uint32_t now = millis();
    // Trigger periodic Modbus scan
    if(now - lastScanTime >= gAcqScanInterval){
      acquisition_scan();
      lastScanTime = now;
    }

    // Wait on the queue for up to 100ms for incoming responses
    if(xQueueReceive(gAcqResponseQueue, &response, pdMS_TO_TICKS(100)) == pdPASS){
      tag_runtime_t* runtime_tag = tag_runtime_get(response.tagId);
      
      if(runtime_tag != NULL){
        if(tag_runtime_lock() == SYS_OK){
          if(response.status == SYS_OK){
            runtime_tag->value = response.value;
            runtime_tag->valid = true;
            runtime_tag->timestamp = millis();
            runtime_tag->changed = true;
          } 
          else {
            runtime_tag->valid = false;
            LOG_ERROR(MODULE, "Protocol read error for tag %s", runtime_tag->config->name);
          }
          tag_runtime_unlock();
        }
      }
    }
  }
}

sys_status_t acquisition_init(const acquisition_config_t* cfg){
  if(gAcqState != ACQ_STATE_DEINITIALIZED){
    LOG_ERROR(MODULE, "Invalid state");
    return SYS_ERR_INVALID_STATE;
  }
  if(cfg == NULL){
    LOG_ERROR(MODULE, "Invalid params");
    return SYS_ERR_INVALID_PARAM;
  }

  LOG_INFO(MODULE, "Initializing...");
  memcpy(&gConfig, cfg, sizeof(acquisition_config_t));
  gAcqScanInterval = gConfig.scanIntervalMs;


  gAcqResponseQueue = xQueueCreate(30, sizeof(protocol_response_t));
  if(gAcqResponseQueue == NULL){
    LOG_ERROR(MODULE, "Failed to create response queue");
    return SYS_ERR_NO_MEMORY;
  }

  gAcqState = ACQ_STATE_INITIALIZED;
  LOG_INFO(MODULE, "Initialize success (%lu ms)", gAcqScanInterval);
  return SYS_OK;
}

sys_status_t acquisition_start(void){
  if(gAcqState != ACQ_STATE_INITIALIZED && gAcqState != ACQ_STATE_STOPPED){
    LOG_ERROR(MODULE, "Invalid state");
    return SYS_ERR_INVALID_STATE;
  }

  LOG_INFO(MODULE, "Starting...");
  if(xTaskCreatePinnedToCore(acquisition_task, "Acquisition", 4096, NULL, 5, &gAcqTaskHandle, 1) != pdPASS) {
    LOG_ERROR(MODULE, "Failed to create task");
    return SYS_ERR_NO_MEMORY;
  }

  gAcqState = ACQ_STATE_RUNNING;
  return SYS_OK;
}

sys_status_t acquisition_stop(void){
  if(gAcqState != ACQ_STATE_RUNNING){
    LOG_ERROR(MODULE, "Invalid State");
    return SYS_ERR_INVALID_STATE;
  }

  LOG_INFO(MODULE, "Stopping...");
  if(gAcqTaskHandle != NULL){
    vTaskDelete(gAcqTaskHandle);
    gAcqTaskHandle = NULL;
  }

  gAcqState = ACQ_STATE_STOPPED;
  return SYS_OK;
}

acq_state_t acquisition_get_state(void){
  return gAcqState;
}