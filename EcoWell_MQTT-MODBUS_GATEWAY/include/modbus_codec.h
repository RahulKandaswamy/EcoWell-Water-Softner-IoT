#ifndef MODBUS_CODEC_H
#define MODBUS_CODEC_H

#include <stdint.h>
#include "tag_registry.h"
#include "tag_runtime.h"

void decode_uint32(const uint16_t* regs, word_order_t order, tag_value_t* value);
void decode_int32(const uint16_t* regs, word_order_t order, tag_value_t* value);

void encode_uint16(const tag_value_t *value, uint16_t *regs);
void encode_int16(const tag_value_t *value, uint16_t *regs);
void encode_uint32(const tag_value_t *value, word_order_t order, uint16_t *regs);
void encode_int32(const tag_value_t *value, word_order_t order, uint16_t *regs);
void encode_float32(const tag_value_t *value, word_order_t order, uint16_t *regs);

#endif