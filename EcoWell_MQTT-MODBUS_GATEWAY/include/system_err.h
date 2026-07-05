#ifndef SYSTEM_ERR_H
#define SYSTEM_ERR_H

typedef enum {
  SYS_OK                = 0,
  SYS_ERR_FAIL          = 1,
  SYS_ERR_INVALID_PARAM = 2,
  SYS_ERR_INVALID_STATE = 3,
  SYS_ERR_NOT_FOUND     = 4,
  SYS_ERR_NO_MEMORY     = 5,
  SYS_ERR_TIMEOUT       = 6,
  SYS_ERR_BUSY          = 7
} sys_status_t;

#endif