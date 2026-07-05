#ifndef MODBUS_SERVICE_H
#define MODBUS_SERVICE_H

#include "system_err.h"
#include "protocol_dispatcher.h"

typedef struct{
  uint32_t baudrate;
  uint32_t serialConfig;
  uint8_t txPin;
  uint8_t rxPin;
  uint16_t timeoutMs;
} modbus_config_t;

sys_status_t modbus_init(const modbus_config_t* cfg);
sys_status_t modbus_read_request(const protocol_request_t* request);
sys_status_t modbus_write_request(const protocol_request_t* request);
sys_status_t modbus_lock(void);
sys_status_t modbus_unlock(void);

#endif