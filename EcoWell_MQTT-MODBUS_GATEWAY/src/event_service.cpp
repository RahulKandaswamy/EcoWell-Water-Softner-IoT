#include "event_service.h"
#include "logger.h"

#define MODULE "EVENT"

static bool gInitialized = false;
static const int eventPostTicksPeriod = 500;

sys_status_t event_init(){
    if (gInitialized){
      LOG_ERROR(MODULE, "Invalid state");
      return SYS_ERR_INVALID_STATE;
    }

    LOG_INFO(MODULE, "Intializing...");
    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE){
      LOG_ERROR(MODULE, "Failed to create default loop (%s)", esp_err_to_name(err));
      return SYS_ERR_FAIL;
    }

    gInitialized = true;
    LOG_INFO(MODULE,"Initialize success");

    return SYS_OK;
}

sys_status_t event_register(esp_event_base_t eventBase, int32_t eventId, esp_event_handler_t handler, void* handlerArg){
  if (!gInitialized){
    return SYS_ERR_INVALID_STATE;
  }
  
  esp_err_t err = esp_event_handler_register(eventBase, eventId, handler, handlerArg);
  if (err != ESP_OK){
    LOG_ERROR(MODULE, "Handler register failed (%s)", esp_err_to_name(err));
    return SYS_ERR_FAIL;
  }

  return SYS_OK;
}

sys_status_t event_unregister(esp_event_base_t eventBase, int32_t eventId, esp_event_handler_t handler){
  if (!gInitialized){
    return SYS_ERR_INVALID_STATE;
  }

  esp_err_t err = esp_event_handler_unregister(eventBase, eventId, handler);
  if (err != ESP_OK){
    LOG_ERROR(MODULE, "Handler unregister failed (%s)", esp_err_to_name(err));
    return SYS_ERR_FAIL;
  }

  return SYS_OK;
}

sys_status_t event_post(esp_event_base_t eventBase, int32_t eventId, void* eventData, size_t eventDataSize){
  if (!gInitialized){
    return SYS_ERR_INVALID_STATE;
  }

  esp_err_t err = esp_event_post(eventBase, eventId, eventData, eventDataSize, pdMS_TO_TICKS(eventPostTicksPeriod));
  if (err != ESP_OK){
    LOG_ERROR(MODULE, "Failed to post event (%s)", esp_err_to_name(err));
    return SYS_ERR_FAIL;
  }

  return SYS_OK;
}