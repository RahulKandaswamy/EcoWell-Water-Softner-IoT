#include "core.h"
#include "logger.h"
#include "tag_registry.h"
#include "tag_runtime.h"
#include "protocol_dispatcher.h"
#include "event_service.h"
#include "system_events.h"

#define MODULE "CORE"

// ===================== STATE =====================
static ws_state_t    gState       = WS_STATE_IDLE;
static portMUX_TYPE  gStateMux    = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t  gTaskHandle  = NULL;
static bool          gInitialized = false;

// ===================== COMMANDS ==================
static SemaphoreHandle_t gCommandMutex   = NULL;
static bool              gRegenPending   = false;
static bool              gAbortPending   = false;

// ===================== REGEN TIMER ===============
static uint32_t gRegenStartTime = 0;
static bool     gRegenStopping  = false;

// ===================== THRESHOLDS ================
static const float    MIN_WATER_PRESSURE  = 20.0f;   // psi
static const float    MIN_SALT_LEVEL      = 10.0f;   // %
static const uint32_t MAX_REGEN_TIME_MS   = 120UL * 1000UL; // 2 min
static const uint32_t CORE_EVAL_PERIOD_MS = 2000;    // 2 seconds

// ============= THREAD-SAFE STATE =================
ws_state_t core_get_state(void){
  ws_state_t copy;
  portENTER_CRITICAL(&gStateMux);
  copy = gState;
  portEXIT_CRITICAL(&gStateMux);
  return copy;
}

static void core_set_state(ws_state_t newState){
  portENTER_CRITICAL(&gStateMux);
  gState = newState;
  portEXIT_CRITICAL(&gStateMux);
}

// ============= REMOTE COMMAND APIs ===============
sys_status_t core_request_regen(void){
  if(!gInitialized) return SYS_ERR_INVALID_STATE;
  xSemaphoreTake(gCommandMutex, portMAX_DELAY);
  gRegenPending = true;
  xSemaphoreGive(gCommandMutex);
  LOG_INFO(MODULE, "Remote regeneration requested");
  return SYS_OK;
}

sys_status_t core_abort_regen(void){
  if(!gInitialized) return SYS_ERR_INVALID_STATE;
  xSemaphoreTake(gCommandMutex, portMAX_DELAY);
  gAbortPending = true;
  xSemaphoreGive(gCommandMutex);
  LOG_INFO(MODULE, "Remote abort requested");
  return SYS_OK;
}

// ============= MODBUS COMMAND ====================
static void send_modbus_command(bool state){
  const tag_config_t* cmd_tag = tag_find_by_name("cmd_regen");
  if(cmd_tag == NULL){
    LOG_ERROR(MODULE, "cmd_regen tag not found!");
    return;
  }

  protocol_request_t req = {};
  req.tag       = cmd_tag;
  req.operation = OP_WRITE;
  req.value.bv  = state;

  if(protocol_dispatcher_post(&req) == SYS_OK){
    LOG_INFO(MODULE, "Modbus command dispatched: %s", state ? "START" : "STOP");
  } else {
    LOG_ERROR(MODULE, "Failed to dispatch Modbus command");
  }
}

// ============= CORE TASK =========================
static void core_task(void* pvParameters){
  LOG_INFO(MODULE, "Task started");

  while(true){
    vTaskDelay(pdMS_TO_TICKS(CORE_EVAL_PERIOD_MS));

    // -------- 1. READ SENSOR DATA --------
    tag_runtime_t* pressure_tag    = tag_runtime_get(2);
    tag_runtime_t* salt_tag        = tag_runtime_get(3);
    tag_runtime_t* regen_stat_tag  = tag_runtime_get(6);

    if(pressure_tag == NULL || salt_tag == NULL || regen_stat_tag == NULL) continue;

    float current_pressure      = 0.0f;
    float current_salt           = 0.0f;
    bool  is_regenerating        = false;
    bool  process_data_valid     = false;

    if(tag_runtime_lock() == SYS_OK){
      if(pressure_tag->valid && salt_tag->valid && regen_stat_tag->valid){
        current_pressure  = pressure_tag->value.f32v;
        current_salt      = salt_tag->value.f32v;
        is_regenerating   = regen_stat_tag->value.bv;
        process_data_valid = true;
      }
      tag_runtime_unlock();
    }

    ws_state_t previousState = core_get_state();
    ws_state_t newState      = previousState;
    bool       system_fault  = false;

    // -------- 2. EVALUATE STATE --------

    // PRIORITY 1: No sensor data → IDLE
    if(!process_data_valid){
      if(previousState != WS_STATE_IDLE){
        LOG_WARN(MODULE, "Process data invalid, entering IDLE");
        event_post(CORE_EVENTS, CORE_EVENT_FAULT_DATA_LOSS, NULL, 0);
      }
      newState = WS_STATE_IDLE;
      system_fault = true;
    }
    // PRIORITY 2: Low pressure → FAULT
    else if(current_pressure < MIN_WATER_PRESSURE){
      if(previousState != WS_STATE_FAULT){
        LOG_ERROR(MODULE, "FAULT: Pressure too low (%.2f psi)", current_pressure);
        event_post(CORE_EVENTS, CORE_EVENT_FAULT_LOW_PRESSURE, NULL, 0);
      }
      newState = WS_STATE_FAULT;
      system_fault = true;
    }
    // PRIORITY 3: Low salt → FAULT
    else if(current_salt < MIN_SALT_LEVEL){
      if(previousState != WS_STATE_FAULT){
        LOG_ERROR(MODULE, "FAULT: Salt level critically low (%.2f %%)", current_salt);
        event_post(CORE_EVENTS, CORE_EVENT_FAULT_LOW_SALT, NULL, 0);
      }
      newState = WS_STATE_FAULT;
      system_fault = true;
    }
    // PRIORITY 4: Machine is actively regenerating
    else if(is_regenerating){
      // Entry: record start time
      if(previousState != WS_STATE_REGEN_RUNNING){
        LOG_INFO(MODULE, "Regeneration cycle started");
        gRegenStartTime = millis();
        gRegenStopping = false;
        event_post(CORE_EVENTS, CORE_EVENT_REGEN_STARTED, NULL, 0);
      }

      // Cycle timer expired → send stop command (once)
      if(millis() - gRegenStartTime >= MAX_REGEN_TIME_MS){
        if(!gRegenStopping){
          LOG_INFO(MODULE, "Cycle time complete. Sending STOP command");
          send_modbus_command(false);
          gRegenStopping = true;
        }
      }
      newState = WS_STATE_REGEN_RUNNING;
    }
    // PRIORITY 5: Normal operation
    else{
      newState = WS_STATE_MONITORING;
    }

    // -------- 3. GLOBAL EXIT TRANSITION --------
    // Fires exactly ONCE when leaving REGEN_RUNNING for ANY reason.
    if(previousState == WS_STATE_REGEN_RUNNING && newState != WS_STATE_REGEN_RUNNING){
      if(system_fault){
        // Fault happened mid-cycle → abort and report failure
        LOG_ERROR(MODULE, "Regeneration aborted due to system fault!");
        send_modbus_command(false);
        event_post(CORE_EVENTS, CORE_EVENT_REGEN_FAILED, NULL, 0);
      }
      else if(gRegenStopping){
        // Cycle timer expired, machine confirmed stopped → success
        LOG_INFO(MODULE, "Regeneration cycle completed successfully");
        event_post(CORE_EVENTS, CORE_EVENT_REGEN_COMPLETED, NULL, 0);
      }
      else{
        // Machine stopped on its own before timer expired (physical button, etc.)
        LOG_WARN(MODULE, "Regeneration stopped by external intervention");
        event_post(CORE_EVENTS, CORE_EVENT_REGEN_COMPLETED, NULL, 0);
      }

      gRegenStopping = false;
      gRegenStartTime = 0;
    }

    // Mid-cycle fault protection: machine is still running but we detected a fault
    if(system_fault && is_regenerating && newState != WS_STATE_REGEN_RUNNING && previousState == WS_STATE_REGEN_RUNNING){
      LOG_WARN(MODULE, "Fault during active regeneration! Force stopping machine");
      send_modbus_command(false);
    }

    core_set_state(newState);

    // -------- 4. PROCESS PENDING COMMANDS --------
    bool execRegen = false;
    bool execAbort = false;

    xSemaphoreTake(gCommandMutex, portMAX_DELAY);
    if(gRegenPending){ execRegen = true; gRegenPending = false; }
    if(gAbortPending){ execAbort = true; gAbortPending = false; }
    xSemaphoreGive(gCommandMutex);

    if(execAbort){
      if(newState == WS_STATE_REGEN_RUNNING){
        LOG_WARN(MODULE, "Cloud abort: Stopping regeneration");
        send_modbus_command(false);
      } else {
        LOG_WARN(MODULE, "Cloud abort: Machine is not regenerating, ignoring");
      }
    }
    else if(execRegen){
      if(!process_data_valid){
        LOG_WARN(MODULE, "Cannot start regen: sensor data invalid");
      }
      else if(system_fault){
        LOG_WARN(MODULE, "Cannot start regen: system is in FAULT");
      }
      else if(newState == WS_STATE_REGEN_RUNNING){
        LOG_WARN(MODULE, "Cannot start regen: already running");
      }
      else{
        LOG_INFO(MODULE, "Safety checks passed. Starting regeneration!");
        send_modbus_command(true);
      }
    }
  }
}

// ============= LIFECYCLE =========================
sys_status_t core_init(void){
  if(gInitialized) return SYS_ERR_INVALID_STATE;
  LOG_INFO(MODULE, "Initializing...");

  gCommandMutex = xSemaphoreCreateMutex();
  if(gCommandMutex == NULL) return SYS_ERR_NO_MEMORY;

  gState = WS_STATE_IDLE;
  gRegenStopping = false;
  gRegenStartTime = 0;
  gInitialized = true;

  LOG_INFO(MODULE, "Initialize success");
  return SYS_OK;
}

sys_status_t core_start(void){
  if(!gInitialized) return SYS_ERR_INVALID_STATE;
  LOG_INFO(MODULE, "Starting...");

  if(xTaskCreatePinnedToCore(core_task, "Core", 4096, NULL, 5, &gTaskHandle, 1) != pdPASS){
    LOG_ERROR(MODULE, "Failed to create task");
    return SYS_ERR_NO_MEMORY;
  }

  LOG_INFO(MODULE, "Started");
  return SYS_OK;
}

sys_status_t core_stop(void){
  if(gTaskHandle != NULL){
    vTaskDelete(gTaskHandle);
    gTaskHandle = NULL;
  }
  core_set_state(WS_STATE_IDLE);
  gRegenStopping = false;
  gRegenStartTime = 0;
  LOG_INFO(MODULE, "Stopped");
  return SYS_OK;
}