#include "tag_registry.h"
#include <string.h>

/* Water Softener Tags */
static tag_config_t gTags[] = {
    {
    .tagId = 1,
    .name = "water_flow",
    .unit = "L/min",
    .topic = "ECO/WS-1/FT-101",
    .topicType = TAG_TOPIC_TYPE_PUBLISH,
    .source = TAG_SOURCE_MODBUS,
    .access = TAG_ACCESS_READ_ONLY,
    .dataType = TAG_FLOAT32,
    .sourceConfig = {
            .modbus = {
                .slaveId = 1, 
                .address = 1, 
                .quantity = 2, 
                .functionCode = 0x03, 
                .wordOrder = WORD_CDAB 
            }
        }
    },  
    {
    .tagId = 2,
    .name = "water_pressure",
    .unit = "psi",
    .topic = "ECO/WS-1/PT-101",
    .topicType = TAG_TOPIC_TYPE_PUBLISH,
    .source = TAG_SOURCE_MODBUS,
    .access = TAG_ACCESS_READ_ONLY,
    .dataType = TAG_FLOAT32,
    .sourceConfig = {
            .modbus = {
                .slaveId = 1, 
                .address = 3, 
                .quantity = 2, 
                .functionCode = 0x03, 
                .wordOrder = WORD_CDAB 
            }
        }
    },
    {
    .tagId = 3,
    .name = "salt_level",
    .unit = "%",
    .topic = "ECO/WS-1/SALT/AT-101",
    .topicType = TAG_TOPIC_TYPE_PUBLISH,
    .source = TAG_SOURCE_MODBUS,
    .access = TAG_ACCESS_READ_ONLY,
    .dataType = TAG_FLOAT32,
    .sourceConfig = {
            .modbus = 
                {
                    .slaveId = 1, 
                    .address = 5, 
                    .quantity = 2, 
                    .functionCode = 0x03, 
                    .wordOrder = WORD_CDAB 
                }
        }
    },
    {
    .tagId = 4,
    .name = "tds",
    .unit = "ppm",
    .topic = "ECO/WS-1/TDS/AT-101",
    .topicType = TAG_TOPIC_TYPE_PUBLISH,
    .source = TAG_SOURCE_MODBUS,
    .access = TAG_ACCESS_READ_ONLY,
    .dataType = TAG_UINT16,
    .sourceConfig = {
            .modbus = 
            {
                .slaveId = 1, 
                .address = 7, 
                .quantity = 1, 
                .functionCode = 0x03, 
                .wordOrder = WORD_ABCD 
            }
        }
    },
    {
    .tagId = 5,
    .name = "power_status",
    .unit = "",
    .topic = "ECO/WS-1/YT-101", 
    .topicType = TAG_TOPIC_TYPE_PUBLISH,
    .source = TAG_SOURCE_MODBUS,
    .access = TAG_ACCESS_READ_ONLY,
    .dataType = TAG_BOOL,
    .sourceConfig = {
            .modbus = 
            { 
                .slaveId = 1, 
                .address = 1,
                .quantity = 1, 
                .functionCode = 0x02, 
                .wordOrder = WORD_ABCD 
            }
        }
    },
    {
    .tagId = 6,
    .name = "regen_status",
    .unit = "",
    .topic = "ECO/WS-1/REGEN_STATE",
    .topicType = TAG_TOPIC_TYPE_PUBLISH,
    .source = TAG_SOURCE_MODBUS,
    .access = TAG_ACCESS_READ_ONLY,
    .dataType = TAG_BOOL,
    .sourceConfig = {
        .modbus = 
            {
                .slaveId = 1, 
                .address = 1, 
                .quantity = 1, 
                .functionCode = 0x02, 
                .wordOrder = WORD_ABCD 
            }
        }
    },
    {
    .tagId = 7,
    .name = "cmd_regen",
    .unit = "",
    .topic = "ECO/WS-1/CMD_REGEN",
    .topicType = TAG_TOPIC_TYPE_SUBSCRIBE,
    .source = TAG_SOURCE_MODBUS,
    .access = TAG_ACCESS_WRITE_ONLY,
    .dataType = TAG_BOOL,
        .sourceConfig = {
            .modbus = 
            {
                .slaveId = 1, 
                .address = 1, 
                .quantity = 1, 
                .functionCode = 0x05, 
                .wordOrder = WORD_ABCD 
            }
        }
    },
    {
    .tagId = 8,
    .name = "core_state",
    .unit = "status",
    .topic = "ECO/WS-1/CORE_STATE",
    .topicType = TAG_TOPIC_TYPE_PUBLISH,
    .source = TAG_SOURCE_SYSTEM,
    .access = TAG_ACCESS_READ_ONLY,
    .dataType = TAG_STRING,
    .sourceConfig = {}
    },
};

static const uint16_t gTagCount = sizeof(gTags) / sizeof(gTags[0]);

uint16_t tag_count(){
  return gTagCount;
}

const tag_config_t* tag_find_by_id(uint16_t tagId){
  for(uint16_t i = 0; i < gTagCount; i++){
    if(gTags[i].tagId == tagId){
      return &gTags[i];
    }
  }
  return NULL;
}

const tag_config_t* tag_find_by_name(const char* name){
  if(name == NULL) return NULL;
  
  for(uint16_t i = 0; i < gTagCount; i++){
    if(gTags[i].name != NULL && strcmp(gTags[i].name, name) == 0){
      return &gTags[i];
    }
  }
  return NULL;
}

const tag_config_t* tag_find_by_topic(const char* topic){
  if(topic == NULL) return NULL;
  
  for(uint16_t i = 0; i < gTagCount; i++){
    if(gTags[i].topic != NULL && strcmp(gTags[i].topic, topic) == 0){
      return &gTags[i];
    }
  }
  return NULL;
}

const tag_config_t* tag_get_at(uint16_t index){
  if(index >= gTagCount){
    return NULL;
  }
  return &gTags[index];
}