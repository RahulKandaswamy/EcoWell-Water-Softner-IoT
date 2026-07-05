#ifndef TAG_REGISTRY_H
#define TAG_REGISTRY_H

#include <Arduino.h>


typedef enum {
    WORD_ABCD = 10,
    WORD_CDAB = 11,
    WORD_BADC = 12,
    WORD_DCBA = 13
} word_order_t;

typedef enum {
    TAG_ACCESS_READ_ONLY = 20,
    TAG_ACCESS_WRITE_ONLY = 21,
    TAG_ACCESS_READ_WRITE = 22
} tag_access_t;

typedef enum {
    TAG_SOURCE_MODBUS = 30,
    TAG_SOURCE_CAN = 31,
    TAG_SOURCE_SYSTEM = 32,
} tag_source_t;

typedef enum {
  TAG_TOPIC_TYPE_SUBSCRIBE = 40,
  TAG_TOPIC_TYPE_PUBLISH = 41 
} tag_topic_type_t;

typedef enum {
    TAG_BOOL = 50,
    TAG_UINT16 = 51,
    TAG_INT16 = 52,
    TAG_UINT32 = 53,
    TAG_INT32 = 54,
    TAG_FLOAT32 = 55,
    TAG_STRING = 56
} tag_type_t;

typedef struct {
  uint8_t slaveId;
  uint16_t address;
  uint16_t quantity;
  uint8_t functionCode;
  word_order_t wordOrder;
} tag_modbus_config_t;

typedef struct {
  uint8_t pin;
} tag_gpio_config_t;

typedef union {
  tag_modbus_config_t modbus;
  tag_gpio_config_t gpio;
} tag_source_config_t;

// LEAN TAG CONFIGURATION
typedef struct {
  uint32_t tagId;
  const char* name;
  const char* unit;
  const char* topic;
  tag_topic_type_t topicType;
  tag_source_t source;
  tag_access_t access;
  tag_type_t dataType;
  tag_source_config_t sourceConfig;
} tag_config_t;

/* API */
const tag_config_t* tag_find_by_id(uint16_t tagId);
const tag_config_t* tag_find_by_name(const char* name);
const tag_config_t* tag_find_by_topic(const char* topic);
const tag_config_t* tag_get_at(uint16_t index);
uint16_t tag_count();

#endif