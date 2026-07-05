#ifndef ACQUISITION_H
#define ACQUISITION_H


#include <Arduino.h>
#include "system_err.h"
#include "protocol_dispatcher.h"

typedef struct {
  uint32_t scanIntervalMs;
} acquisition_config_t;

typedef enum {
  ACQ_STATE_DEINITIALIZED = 300,
  ACQ_STATE_INITIALIZED = 301,
  ACQ_STATE_RUNNING = 302,
  ACQ_STATE_STOPPED = 303
} acq_state_t;

sys_status_t acquisition_init(const acquisition_config_t* cfg);
sys_status_t acquisition_start(void);
sys_status_t acquisition_stop(void);
acq_state_t acquisition_get_state(void);

// Public API for protocol services to post responses back to Acquisition Engine
sys_status_t acquisition_post_response(const protocol_response_t* response);

#endif