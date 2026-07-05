#ifndef SUBSCRIBER_H
#define SUBSCRIBER_H

#include <Arduino.h>
#include "system_err.h"
#include "mqtt_service.h"
#include "tag_registry.h"
#include "tag_runtime.h"

typedef enum{
  SUBSCRIBER_STATE_DEINITIALIZED = 600,
  SUBSCRIBER_STATE_INITIALIZED = 601,
  SUBSCRIBER_STATE_RUNNING = 602,
  SUBSCRIBER_STATE_STOPPED = 603
} subscriber_state_t;

sys_status_t subscriber_init();
sys_status_t subscriber_start(void);
sys_status_t subscriber_stop(void);
sys_status_t subscriber_post(const mqtt_msg_t *msg);

subscriber_state_t subscriber_get_state(void);

#endif