#include "modbus_codec.h"

void decode_uint32(const uint16_t* regs, word_order_t order, tag_value_t* value){
  uint32_t data;
  switch(order){
    case WORD_ABCD:
      data = ((uint32_t)regs[0] << 16) | regs[1];
      break;
    case WORD_CDAB:
      data = ((uint32_t)regs[1] << 16) | regs[0];
      break;
    default:
      data = ((uint32_t)regs[0] << 16) | regs[1];
      break;
  }
  value->u32v = data;
}

void decode_int32(const uint16_t* regs, word_order_t order, tag_value_t* value){
  uint32_t data;
  switch(order){
    case WORD_ABCD:
      data = ((uint32_t)regs[0] << 16) | regs[1];
      break; 
    case WORD_CDAB:
      data = ((uint32_t)regs[1] << 16) | regs[0];
      break;
    default:
      data = ((uint32_t)regs[0] << 16) | regs[1];
      break;
  }
  value->i32v = (int32_t)data;
}

void encode_uint16(const tag_value_t *value, uint16_t *regs){
  regs[0] = value->u16v;
}

void encode_int16(const tag_value_t *value, uint16_t *regs){
  regs[0] = (uint16_t)value->i16v;
}

void encode_uint32(const tag_value_t *value, word_order_t order, uint16_t *regs){
  switch(order){
    case WORD_ABCD:
      regs[0] = (uint16_t)(value->u32v >> 16);
      regs[1] = (uint16_t)(value->u32v);
      break;
    case WORD_CDAB:
      regs[1] = (uint16_t)(value->u32v >> 16);
      regs[0] = (uint16_t)(value->u32v);
      break;
    default:
      regs[0] = (uint16_t)(value->u32v >> 16);
      regs[1] = (uint16_t)(value->u32v);
      break;
  }
}

void encode_int32(const tag_value_t *value, word_order_t order, uint16_t *regs){
  uint32_t data = (uint32_t)value->i32v;
  switch(order){
    case WORD_ABCD:
      regs[0] = (uint16_t)(data >> 16);
      regs[1] = (uint16_t)data;
      break;
    case WORD_CDAB:
      regs[1] = (uint16_t)(data >> 16);
      regs[0] = (uint16_t)data;
      break;
    default:
      regs[0] = (uint16_t)(data >> 16);
      regs[1] = (uint16_t)data;
      break;
  }
}

void encode_float32(const tag_value_t *value, word_order_t order, uint16_t *regs){
    union {
        float f;
        uint32_t u;
    } conv;
    conv.f = value->f32v;

    switch(order) {
        case WORD_ABCD:
            regs[0] = (uint16_t)(conv.u >> 16);
            regs[1] = (uint16_t)conv.u;
            break;
        case WORD_CDAB:
            regs[1] = (uint16_t)(conv.u >> 16);
            regs[0] = (uint16_t)conv.u;
            break;
        default:
            regs[0] = (uint16_t)(conv.u >> 16);
            regs[1] = (uint16_t)conv.u;
            break;
    }
}