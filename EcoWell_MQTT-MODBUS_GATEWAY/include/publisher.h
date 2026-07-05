#ifndef PUBLISHER_H
#define PUBLISHER_H

#include <Arduino.h>
#include "system_err.h"

typedef struct {
  uint32_t publishIntervalMs;
} publisher_config_t;

typedef enum{
  PUBLISHER_STATE_DEINITIALIZED = 500,
  PUBLISHER_STATE_INITIALIZED = 501,
  PUBLISHER_STATE_RUNNING = 502,
  PUBLISHER_STATE_STOPPED = 503
} publisher_state_t;

sys_status_t publisher_init(const publisher_config_t *cfg);
sys_status_t publisher_start(void);
sys_status_t publisher_stop(void);
publisher_state_t publisher_get_state(void);

#endif