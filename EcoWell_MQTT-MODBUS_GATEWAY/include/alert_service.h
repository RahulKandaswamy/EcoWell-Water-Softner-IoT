#ifndef ALERT_SERVICE_H
#define ALERT_SERVICE_H

#include "system_err.h"

sys_status_t alert_init(void);
sys_status_t alert_start(void);
sys_status_t alert_stop(void);

#endif