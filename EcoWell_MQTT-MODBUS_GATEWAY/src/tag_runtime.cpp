#include "tag_runtime.h"
#include "logger.h"
#include <stdlib.h>

#define MODULE "TAG_RUNTIME"

static tag_runtime_t* gTagRuntime = NULL;
static SemaphoreHandle_t gRuntimeMutex = NULL;
static bool gInitialized = false;

sys_status_t tag_runtime_init(){
  if(gInitialized){
    LOG_ERROR(MODULE, "Invalid state");
    return SYS_ERR_INVALID_STATE;
  }

  LOG_INFO(MODULE, "Intializing...");
  gTagRuntime = (tag_runtime_t*)calloc(tag_count(), sizeof(tag_runtime_t));
  if(gTagRuntime == NULL){
    LOG_ERROR(MODULE, "Failed to allocate storage for runtime tags");
    return SYS_ERR_NO_MEMORY;
  }
  
  for(uint16_t i = 0; i < tag_count(); i++){
    gTagRuntime[i].config = tag_get_at(i);
    gTagRuntime[i].valid = false;
    gTagRuntime[i].changed = false;
    gTagRuntime[i].timestamp = 0;
  }

  gRuntimeMutex = xSemaphoreCreateMutex();
  if(gRuntimeMutex == NULL){
    free(gTagRuntime);
    LOG_ERROR(MODULE, "Failed to create mutex");
    return SYS_ERR_NO_MEMORY;
  }

  LOG_INFO(MODULE, "Initialize success, allocated storage for %u runtime tags", tag_count());
  gInitialized = true;
  return SYS_OK;
}

sys_status_t tag_runtime_lock(void){
  if(!gInitialized) return SYS_ERR_INVALID_STATE;
  if(xSemaphoreTake(gRuntimeMutex, portMAX_DELAY) != pdTRUE) return SYS_ERR_FAIL;
  
  return SYS_OK;
}

sys_status_t tag_runtime_unlock(void){
  if(!gInitialized) return SYS_ERR_INVALID_STATE;
  xSemaphoreGive(gRuntimeMutex);
  return SYS_OK;
}

tag_runtime_t* tag_runtime_get(uint32_t tagId){
  if(!gInitialized){
    LOG_ERROR(MODULE, "Tag runtime not initialized");
    return NULL;
  }
  for(uint16_t i = 0; i < tag_count(); i++){
    if(gTagRuntime[i].config->tagId == tagId){
      return &gTagRuntime[i];
    }
  }
  return NULL;
}

tag_runtime_t* tag_runtime_get_at(uint16_t idx){
  if(!gInitialized){
    LOG_ERROR(MODULE, "Tag runtime not initialized");
    return NULL;
  }
  if(idx >= tag_count()){
    LOG_ERROR(MODULE, "Invalid tag position");
    return NULL;
  }
  return &gTagRuntime[idx];
}

void tag_runtime_print(const tag_runtime_t* tag){
  if(tag == NULL) return;
  if(tag->config == NULL) return;

  if(!tag->valid){
    LOG_WARN(MODULE, "[%s] INVALID", tag->config->name);
    return;
  }

  switch(tag->config->dataType){
    case TAG_BOOL:{
      LOG_INFO(MODULE, "[%s] = %s", tag->config->name, tag->value.bv ? "TRUE" : "FALSE");
      break;
    }
    case TAG_UINT16:{
      LOG_INFO(MODULE, "[%s] = %u %s", tag->config->name, tag->value.u16v, tag->config->unit);
      break;
    }
    case TAG_INT16:{
      LOG_INFO(MODULE, "[%s] = %d %s", tag->config->name, tag->value.i16v, tag->config->unit);
      break;
    }
    case TAG_UINT32:{
      LOG_INFO(MODULE, "[%s] = %lu %s", tag->config->name, tag->value.u32v, tag->config->unit);
      break;
    }
    case TAG_INT32:{
      LOG_INFO(MODULE, "[%s] = %ld %s", tag->config->name, tag->value.i32v, tag->config->unit);
      break;
    }
    case TAG_FLOAT32:{
      LOG_INFO(MODULE, "[%s] = %.2f %s", tag->config->name, tag->value.f32v, tag->config->unit);
      break;
    }
    case TAG_STRING:{
      LOG_INFO(MODULE, "[%s] = %s", tag->config->name, tag->value.strv);
        break;
      } 
    default:{
      LOG_WARN(MODULE, "[%s] Unknown datatype", tag->config->name);
      break;
    }
  }
}