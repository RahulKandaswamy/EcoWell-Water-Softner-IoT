#include "protocol_dispatcher.h"
#include "modbus_service.h"
#include "logger.h"
#include "system_service.h"

#define MODULE "PROTOCOL-DISPATCHER"

static QueueHandle_t gQueue = NULL;
static TaskHandle_t gTaskHandle = NULL;
static protocol_dispatcher_state_t gState = PROTOCOL_DISPATCHER_STATE_DEINITIALIZED;

protocol_dispatcher_state_t protocol_dispatcher_get_state(){
  return gState;
}

sys_status_t protocol_dispatcher_post(const protocol_request_t *request){
  if(gState != PROTOCOL_DISPATCHER_STATE_RUNNING){
    LOG_ERROR(MODULE, "Invalid state");
    return SYS_ERR_INVALID_STATE;
  }

  if(request == NULL){
    LOG_ERROR(MODULE, "Invalid parameter");
    return SYS_ERR_INVALID_PARAM;
  }

  if(xQueueSend(gQueue, request, 500) != pdPASS){
    LOG_WARN(MODULE, "Queue full");
    return SYS_ERR_BUSY;
  }

  return SYS_OK;
}

static sys_status_t dispatch_request(const protocol_request_t *request){
  if(request == NULL){
    LOG_ERROR(MODULE, "Invalid params");
    return SYS_ERR_INVALID_PARAM;
  }

  switch(request->tag->source){
    case TAG_SOURCE_MODBUS:
      if(request->operation == OP_READ){
        if(modbus_read_request(request) != SYS_OK){
          LOG_ERROR(MODULE, "Modbus read failed for %s", request->tag->name);
          return SYS_ERR_FAIL;
        }
      } 
      else if (request->operation == OP_WRITE) {
        LOG_INFO(MODULE, "Dispatching MODBUS write : %s", request->tag->name);
        if(modbus_write_request(request) != SYS_OK){
          LOG_ERROR(MODULE, "Modbus write failed for %s", request->tag->name);
          return SYS_ERR_FAIL;
        }
      }
      break;

    case TAG_SOURCE_SYSTEM:
      if (request->operation == OP_READ) {
        if (system_service_read_request(request) != SYS_OK) {
          LOG_ERROR(MODULE, "System read failed for %s", request->tag->name);
          return SYS_ERR_FAIL;
        }
      }
      break;

    case TAG_SOURCE_CAN:
      LOG_INFO(MODULE, "Dispatching CAN request : %s", request->tag->name);
      break;

    default:
      LOG_ERROR(MODULE, "Unsupported source");
      return SYS_ERR_FAIL;
  }

  return SYS_OK;
}

static void protocolDispatcherTask(void *pv){
  protocol_request_t request;
  LOG_INFO(MODULE, "Task started");
  while(true){
    if(xQueueReceive(gQueue, &request, portMAX_DELAY) != pdPASS){
      LOG_ERROR(MODULE, "Failed to receive the request");
      continue;
    }

    if(dispatch_request(&request) != SYS_OK){
      LOG_ERROR(MODULE, "Failed to dispatch the request");
    }
  }
}

sys_status_t protocol_dispatcher_init(){
  if(gState != PROTOCOL_DISPATCHER_STATE_DEINITIALIZED){
    LOG_ERROR(MODULE, "Invalid state");
    return SYS_ERR_INVALID_STATE;
  }

  LOG_INFO(MODULE, "Initializing...");
  gQueue = xQueueCreate(30, sizeof(protocol_request_t));

  if(gQueue == NULL){
    LOG_ERROR(MODULE, "Queue creation failed");
    return SYS_ERR_NO_MEMORY;
  }

  gState = PROTOCOL_DISPATCHER_STATE_INITIALIZED;
  LOG_INFO(MODULE, "Initialize success");
  return SYS_OK;
}

sys_status_t protocol_dispatcher_start(){
  if(gState != PROTOCOL_DISPATCHER_STATE_INITIALIZED && gState != PROTOCOL_DISPATCHER_STATE_STOPPED){
    LOG_ERROR(MODULE, "Invalid state");
    return SYS_ERR_INVALID_STATE;
  }

  LOG_INFO(MODULE, "Starting...");
  // Note: Adjust STACK_LARGE / PRIORITY_NORMAL based on your project definitions, or just use raw numbers like 4096 and 5
  if(xTaskCreatePinnedToCore(protocolDispatcherTask, "protocol_dispatcher", 4096, NULL, 5, &gTaskHandle, 1) != pdPASS){
    LOG_ERROR(MODULE, "Failed to create task");
    return SYS_ERR_NO_MEMORY;
  }

  gState = PROTOCOL_DISPATCHER_STATE_RUNNING;
  return SYS_OK;
}

sys_status_t protocol_dispatcher_stop(){
  if(gState != PROTOCOL_DISPATCHER_STATE_RUNNING){
    LOG_ERROR(MODULE, "Invalid state");
    return SYS_ERR_INVALID_STATE;
  }

  LOG_INFO(MODULE, "Stopping...");
  if(gTaskHandle != NULL){
    vTaskDelete(gTaskHandle);
    gTaskHandle = NULL;
  }

  gState = PROTOCOL_DISPATCHER_STATE_STOPPED;
  LOG_INFO(MODULE, "Stopped");
  return SYS_OK;
}