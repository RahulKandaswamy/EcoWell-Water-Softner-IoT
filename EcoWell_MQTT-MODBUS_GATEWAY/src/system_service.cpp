#include "system_service.h"
#include "logger.h"
#include "acquisition.h"
#include "core.h"
#include "mqtt_service.h"
// #include "wifi_service.h" 
#include <string.h>

#define MODULE "SYSTEM-SVC"

sys_status_t system_service_read_request(const protocol_request_t* request){
  if(request == NULL || request->tag == NULL) return SYS_ERR_INVALID_PARAM;
  
  // Ensure we are only handling internal system requests
  if(request->tag->source != TAG_SOURCE_SYSTEM) return SYS_ERR_INVALID_PARAM;

  // Build a standard protocol response
  protocol_response_t p_resp;
  p_resp.tagId = request->tag->tagId;
  p_resp.status = SYS_OK;
  
  // Route to the appropriate module to fetch the state
  if (strcmp(request->tag->name, "core_state") == 0) {
    uint16_t state = (uint16_t)core_get_state();

    switch(request->tag->dataType) {
      case TAG_UINT16:
        p_resp.value.u16v = state;
        break;

      case TAG_STRING:
        if(state == WS_STATE_IDLE) {
          strncpy(p_resp.value.strv, "IDLE", sizeof(p_resp.value.strv)-1);
        } 
        else if(state == WS_STATE_REGEN_RUNNING) {
          strncpy(p_resp.value.strv, "REGEN_RUNNING", sizeof(p_resp.value.strv)-1);
        }
        else if(state == WS_STATE_MONITORING){
          strncpy(p_resp.value.strv, "MONITORING", sizeof(p_resp.value.strv)-1);
        }
        else if(state == WS_STATE_FAULT){
          strncpy(p_resp.value.strv, "FAULT", sizeof(p_resp.value.strv)-1);
        }
        else {
          strncpy(p_resp.value.strv, "UNKNOWN", sizeof(p_resp.value.strv)-1);
        }
        break;

      default:
        LOG_WARN(MODULE, "Unsupported data type for core_state: %d", request->tag->dataType);
        p_resp.status = SYS_ERR_INVALID_PARAM;
        break;
    }
  }
  else {
    LOG_WARN(MODULE, "Unknown system tag: %s", request->tag->name);
    p_resp.status = SYS_ERR_NOT_FOUND;
  }

  return acquisition_post_response(&p_resp);
}