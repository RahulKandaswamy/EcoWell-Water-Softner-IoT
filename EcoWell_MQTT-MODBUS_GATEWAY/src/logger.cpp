#include "logger.h"
#include <stdarg.h>

#define MODULE "LOGGER"

static bool gInitialized = false;
static SemaphoreHandle_t gLoggerMutex = NULL;

sys_status_t logger_init(void){
  if(gInitialized){
    Serial.println("[ERROR] [LOGGER] Invalid state");
    return SYS_ERR_INVALID_STATE;
  } 

  Serial.begin(115200);
  while (!Serial) {}
  Serial.println("[INFO] [LOGGER] __ OK __");

  Serial.println("[INFO] [LOGGER] Initializing...");

  gLoggerMutex = xSemaphoreCreateMutex();
  if (gLoggerMutex == NULL) {
    Serial.println("[ERROR] [LOGGER] logger mutex create unsuccess");
    return SYS_ERR_NO_MEMORY;
  }

  Serial.println("[INFO] [LOGGER] Initialize success");
  gInitialized = true;
  return SYS_OK;
}

void logger_log(log_level_t level, const char* module, const char* format, ...){
  if(!gInitialized){
    Serial.println("[ERROR] [LOGGER] Logger not initialized");
    return;
  }

  xSemaphoreTake(gLoggerMutex, portMAX_DELAY);
  char message[256];
  va_list args;
  va_start(args, format);
  vsnprintf(message, sizeof(message), format, args);
  va_end(args);

  const char* levelStr = "";
  switch(level){
    case LOG_LEVEL_INFO:
      levelStr = "INFO";
      break;

    case LOG_LEVEL_WARN:
      levelStr = "WARN";
      break;

    case LOG_LEVEL_ERROR:
      levelStr = "ERROR";
      break;

    case LOG_LEVEL_DEBUG:
      levelStr = "DEBUG";
      break;

    default:
      levelStr = "UNKNOWN";
      break;
  }
  Serial.printf("[%s] [%s] %s\n", levelStr, module, message);

  xSemaphoreGive(gLoggerMutex);
}