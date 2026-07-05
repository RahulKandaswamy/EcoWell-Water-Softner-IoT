#ifndef CORE_H
#define CORE_H

#include <Arduino.h>
#include "system_err.h"

typedef enum {
  WS_STATE_IDLE = 400,
  WS_STATE_MONITORING = 401,
  WS_STATE_REGEN_REQUIRED = 402,
  WS_STATE_REGEN_RUNNING = 403,
  WS_STATE_FAULT = 404
} ws_state_t;

sys_status_t core_init(void);
sys_status_t core_start(void);
sys_status_t core_stop(void);
ws_state_t core_get_state(void);
sys_status_t core_request_regen(void);
sys_status_t core_abort_regen(void);

#endif