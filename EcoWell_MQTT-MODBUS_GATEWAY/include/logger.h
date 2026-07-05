#ifndef LOGGER_H
#define LOGGER_H

#include <Arduino.h>
#include "system_err.h"
#include "modbus_codec.h"  
#include "logger.h"
#include "acquisition.h"
#include <ModbusClientRTU.h>
#include <ModbusError.h>

#define LOG_INFO(module, fmt, ...)  logger_log(LOG_LEVEL_INFO,  module, fmt, ##__VA_ARGS__)
#define LOG_WARN(module, fmt, ...)  logger_log(LOG_LEVEL_WARN,  module, fmt, ##__VA_ARGS__)
#define LOG_ERROR(module, fmt, ...) logger_log(LOG_LEVEL_ERROR, module, fmt, ##__VA_ARGS__)
#define LOG_DEBUG(module, fmt, ...) logger_log(LOG_LEVEL_DEBUG, module, fmt, ##__VA_ARGS__)

typedef enum{
  LOG_LEVEL_INFO = 100,
  LOG_LEVEL_WARN = 102,
  LOG_LEVEL_ERROR = 103,
  LOG_LEVEL_DEBUG = 104
} log_level_t;

sys_status_t logger_init(void);
void logger_log(
    log_level_t level,
    const char* module,
    const char* format,
    ...
);
#endif