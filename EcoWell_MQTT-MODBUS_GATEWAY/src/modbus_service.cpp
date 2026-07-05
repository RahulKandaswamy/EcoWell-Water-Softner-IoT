#include <ModbusClientRTU.h>
#include <ModbusError.h>
#include "modbus_service.h"
#include "tag_registry.h"
#include "tag_runtime.h"
#include "logger.h"
#include "modbus_codec.h"

#define MODULE "MODBUS"

static bool gInitialized = false;
static modbus_config_t gConfig;
static ModbusClientRTU modbus(21);
static SemaphoreHandle_t gModbusMutex = NULL;

void on_modbus_data(ModbusMessage response, uint32_t token){
  // 1. Look up the STATIC config to know how to decode (NO runtime lock needed!)
  const tag_config_t* tag = tag_find_by_id(token);
  if(tag == NULL){
    LOG_ERROR(MODULE, "Received data for unknown tag ID: %lu", token);
    return;
  }

  tag_value_t temp_value;
  switch(tag->dataType){
    case TAG_BOOL:{
      uint8_t byteData;
      response.get(3, byteData);
      temp_value.bv = (byteData & 0x01);
      break;
    } 
    case TAG_UINT16:{
      uint16_t reg;
      response.get(3, reg);
      temp_value.u16v = reg;
      break;
    }
    case TAG_INT16:{
      uint16_t reg;
      response.get(3, reg);
      temp_value.i16v = (int16_t)reg;
      break;
    }
    case TAG_UINT32:{
      uint16_t regs[2];
      response.get(3, regs[0]);
      response.get(5, regs[1]);
      decode_uint32(regs, tag->sourceConfig.modbus.wordOrder, &temp_value);
      break;
    }
    case TAG_INT32:{
      uint16_t regs[2];
      response.get(3, regs[0]);
      response.get(5, regs[1]);
      decode_int32(regs, tag->sourceConfig.modbus.wordOrder, &temp_value);
      break;
    }
    case TAG_FLOAT32:{
      float reg;
      response.get(3, reg);
      temp_value.f32v = reg;
      break;
    }
    default:
      LOG_ERROR(MODULE, "Unhandled source dtype for %s", tag->name);
      return;
  }

  protocol_response_t p_resp;
  p_resp.tagId = token;
  p_resp.value = temp_value;
  p_resp.status = SYS_OK;
  acquisition_post_response(&p_resp);
}

void on_modbus_error(Error error, uint32_t token) {
  LOG_ERROR(MODULE, "token %u, Modbus error: %u - %s", token, (int)error, (const char*)ModbusError(error));
  
  protocol_response_t p_resp;
  p_resp.tagId = token;
  p_resp.status = SYS_ERR_FAIL;
  acquisition_post_response(&p_resp);
}

sys_status_t modbus_lock(void){
  if(!gInitialized) return SYS_ERR_INVALID_STATE;
  if(xSemaphoreTake(gModbusMutex, portMAX_DELAY) != pdTRUE) return SYS_ERR_FAIL;
  return SYS_OK;
}

sys_status_t modbus_unlock(void){
  if(!gInitialized) return SYS_ERR_INVALID_STATE;
  if(xSemaphoreGive(gModbusMutex) != pdTRUE) return SYS_ERR_FAIL;
  return SYS_OK;
}

sys_status_t modbus_init(const modbus_config_t* cfg){
  if(gInitialized){
    LOG_ERROR(MODULE, "Invalid state");
    return SYS_ERR_INVALID_STATE;
  }

  if(cfg == NULL){
    LOG_ERROR(MODULE, "Invalid params");
    return SYS_ERR_INVALID_PARAM;
  }

  LOG_INFO(MODULE, "Initializing...");
  memcpy(&gConfig, cfg, sizeof(modbus_config_t));

  gModbusMutex = xSemaphoreCreateMutex();
  if(gModbusMutex == NULL){
    LOG_ERROR(MODULE, "Failed to create mutex");
    return SYS_ERR_NO_MEMORY;
  }

  RTUutils::prepareHardwareSerial(Serial1);
  Serial1.begin(gConfig.baudrate, gConfig.serialConfig, gConfig.rxPin, gConfig.txPin);
  modbus.setTimeout(gConfig.timeoutMs);
  modbus.onDataHandler(&on_modbus_data);
  modbus.onErrorHandler(&on_modbus_error);
  modbus.begin(Serial1, 0);
  
  gInitialized = true;
  LOG_INFO(MODULE, "Initialized success");
  return SYS_OK;
}

sys_status_t modbus_read_request(const protocol_request_t* request){
  if(!gInitialized) {
    LOG_ERROR(MODULE, "Invalid state");
    return SYS_ERR_INVALID_STATE;
  }
  if(request == NULL || request->tag == NULL) {
    LOG_ERROR(MODULE, "Invalid params");
    return SYS_ERR_INVALID_PARAM;
  }
  if(request->tag->source != TAG_SOURCE_MODBUS) {
    LOG_ERROR(MODULE, "Invalid tag source");
    return SYS_ERR_INVALID_PARAM;
  }

  const tag_modbus_config_t* cfg = &request->tag->sourceConfig.modbus;
  
  Error err;
  if(modbus_lock() != SYS_OK) return SYS_ERR_FAIL;
  err = modbus.addRequest(request->tag->tagId, cfg->slaveId, cfg->functionCode, cfg->address, cfg->quantity);
  if(modbus_unlock() != SYS_OK) return SYS_ERR_FAIL;

  if(err != SUCCESS){
    LOG_ERROR(MODULE, "Error creating request: %02X - %s", (int)err, (const char *)ModbusError(err));
    return SYS_ERR_FAIL;
  }
  return SYS_OK;
}

sys_status_t modbus_write_request(const protocol_request_t* request){
  if(!gInitialized){
    LOG_ERROR(MODULE, "Invalid state");
    return SYS_ERR_INVALID_STATE;
  }
  if(request == NULL || request->tag == NULL){
    LOG_ERROR(MODULE, "Invalid params");
    return SYS_ERR_INVALID_PARAM;
  }
  if(request->tag->source != TAG_SOURCE_MODBUS){
    LOG_ERROR(MODULE, "Invalid tag source");
    return SYS_ERR_INVALID_PARAM;
  }

  const tag_modbus_config_t *cfg = &request->tag->sourceConfig.modbus;
  uint16_t regs[2] = {0};

  if(cfg->quantity == 0 || cfg->quantity > 2) {
    LOG_ERROR(MODULE, "Invalid quantity");
    return SYS_ERR_INVALID_PARAM;
  }

  switch(request->tag->dataType){
    case TAG_UINT16: encode_uint16(&request->value, regs); break;
    case TAG_INT16:  encode_int16(&request->value, regs); break;
    case TAG_UINT32: encode_uint32(&request->value, cfg->wordOrder, regs); break;
    case TAG_INT32:  encode_int32(&request->value, cfg->wordOrder, regs); break;
    case TAG_FLOAT32: encode_float32(&request->value, cfg->wordOrder, regs); break;
    case TAG_BOOL: break; // Handled below
    default: return SYS_ERR_FAIL;
  }

  Error err;
  if(cfg->functionCode == 0x06){
    if(modbus_lock() != SYS_OK) return SYS_ERR_FAIL;
    err = modbus.addRequest(request->tag->tagId, cfg->slaveId, 0x06, cfg->address, regs[0]);
    if(modbus_unlock() != SYS_OK) return SYS_ERR_FAIL;
  }
  else if(cfg->functionCode == 0x10){
    if(modbus_lock() != SYS_OK) return SYS_ERR_FAIL;
    err = modbus.addRequest(request->tag->tagId, cfg->slaveId, 0x10, cfg->address, cfg->quantity, cfg->quantity * 2, regs);
    if(modbus_unlock() != SYS_OK) return SYS_ERR_FAIL;
  }
  else if(cfg->functionCode == 0x05){
    if(modbus_lock() != SYS_OK) return SYS_ERR_FAIL;
    uint16_t coilValue = request->value.bv ? 0xFF00 : 0x0000;
    err = modbus.addRequest(request->tag->tagId, cfg->slaveId, 0x05, cfg->address, coilValue);
    if(modbus_unlock() != SYS_OK) return SYS_ERR_FAIL;
  }
  else{
    err = ILLEGAL_FUNCTION;
  }

  if(err != SUCCESS){
    LOG_ERROR(MODULE, "Write error: %02X - %s", (int)err, (const char *)ModbusError(err));
    return SYS_ERR_FAIL;
  }
  return SYS_OK;
}