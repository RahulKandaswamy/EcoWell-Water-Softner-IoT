#ifndef EVENT_SERVICE_H
#define EVENT_SERVICE_H

#include <esp_event.h>
#include "system_err.h"

sys_status_t event_init(void);
sys_status_t event_register(esp_event_base_t eventBase, int32_t eventId, esp_event_handler_t handler, void* handlerArg);
sys_status_t event_unregister(esp_event_base_t eventBase, int32_t eventId, esp_event_handler_t handler);
sys_status_t event_post(esp_event_base_t eventBase, int32_t eventId, void* eventData, size_t eventDataSize);

#endif