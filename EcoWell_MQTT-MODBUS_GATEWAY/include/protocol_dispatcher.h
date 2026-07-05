#ifndef PROTOCOL_DISPATCHER_H
#define PROTOCOL_DISPATCHER_H

#include <Arduino.h>
#include "system_err.h"
#include "tag_registry.h"
#include "tag_runtime.h"

// Define the state of the dispatcher
typedef enum {
  PROTOCOL_DISPATCHER_STATE_DEINITIALIZED = 200,
  PROTOCOL_DISPATCHER_STATE_INITIALIZED = 201,
  PROTOCOL_DISPATCHER_STATE_RUNNING = 202,
  PROTOCOL_DISPATCHER_STATE_STOPPED = 203
} protocol_dispatcher_state_t;

// Define operation types
typedef enum {
  OP_READ,
  OP_WRITE
} protocol_op_t;

// The generic Request sent TO the dispatcher
typedef struct {
  const tag_config_t *tag;
  protocol_op_t operation;
  tag_value_t value; // Only used for OP_WRITE
} protocol_request_t;

// The generic Response returned FROM a protocol service
typedef struct {
  uint32_t tagId;
  sys_status_t status;
  tag_value_t value; // Only valid on successful OP_READ
} protocol_response_t;

// APIs
sys_status_t protocol_dispatcher_init(void);
sys_status_t protocol_dispatcher_start(void);
sys_status_t protocol_dispatcher_stop(void);
protocol_dispatcher_state_t protocol_dispatcher_get_state(void);
sys_status_t protocol_dispatcher_post(const protocol_request_t *request);

#endif