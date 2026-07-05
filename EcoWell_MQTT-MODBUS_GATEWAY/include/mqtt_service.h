#ifndef MQTT_SERVICE_H
#define MQTT_SERVICE_H

#include "system_err.h"

#define MQTT_MAX_TOPIC_LENGTH  128
#define MQTT_MAX_PAYLOAD_LENGTH 512

typedef enum{
    MQTT_STATE_DEINITIALIZED = 610,
    MQTT_STATE_INITIALIZED = 611,
    MQTT_STATE_CONNECTING = 612,
    MQTT_STATE_RUNNING = 613,
    MQTT_STATE_STOPPED = 614
} mqtt_state_t;

typedef struct{
    const char* broker;
    uint16_t port;
    const char* username;
    const char* password;
    const char* clientId;
    bool secure;
    const char* caCert;
    bool cleanSession;
    bool autoReconnect;
    uint16_t keepAlive;
} mqtt_config_t;

typedef struct{
    char topic[MQTT_MAX_TOPIC_LENGTH];
    char payload[MQTT_MAX_PAYLOAD_LENGTH];
    uint8_t qos;
    bool retain;
    size_t len;
} mqtt_msg_t;

sys_status_t mqtt_init(const mqtt_config_t* cfg);
sys_status_t mqtt_start(void);
sys_status_t mqtt_stop(void);
sys_status_t mqtt_publish(const mqtt_msg_t *msg);
sys_status_t mqtt_subscribe(const char *topic, uint8_t qos);

// QueueHandle_t mqtt_get_tx_queue(void);
mqtt_state_t mqtt_get_state(void);

#endif