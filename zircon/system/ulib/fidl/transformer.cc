// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/internal.h>
#include <lib/fidl/transformer.h>

#include <algorithm>
#include <cstdint>
#include <cstring>

#define SRC_VALUE(v1_value, v2_value) \
  ((transformation_ == FIDL_TRANSFORMATION_V1_TO_V2) ? (v1_value) : (v2_value))
#define DST_VALUE(v1_value, v2_value) \
  ((transformation_ == FIDL_TRANSFORMATION_V1_TO_V2) ? (v2_value) : (v1_value))

namespace {

struct WireEnvelopeV1 {
  uint32_t num_bytes;
  uint32_t num_handles;
  uint64_t presence_marker;
};

struct WireEnvelopeV2 {
  union {
    uint32_t num_bytes;
    uint8_t inlined_value[4];
  };
  uint16_t num_handles;
  uint16_t flags;
};

constexpr uint16_t kEmptyFlags = 0x00;
constexpr uint16_t kInlinedEnvelopeFlag = 0x01;
constexpr bool ValidFlags(const WireEnvelopeV2 envelope) {
  return (envelope.flags & ~kInlinedEnvelopeFlag) == 0;
}
constexpr bool IsInlined(const WireEnvelopeV2 envelope) {
  return (envelope.flags & kInlinedEnvelopeFlag) != 0;
}

struct WireTableHeader {
  uint64_t count;
  uint64_t presence_marker;
};

constexpr uint32_t PrimitiveSize(const FidlCodedPrimitiveSubtype primitive) {
  switch (primitive) {
    case kFidlCodedPrimitiveSubtype_Bool:
    case kFidlCodedPrimitiveSubtype_Int8:
    case kFidlCodedPrimitiveSubtype_Uint8:
      return 1;
    case kFidlCodedPrimitiveSubtype_Int16:
    case kFidlCodedPrimitiveSubtype_Uint16:
      return 2;
    case kFidlCodedPrimitiveSubtype_Int32:
    case kFidlCodedPrimitiveSubtype_Uint32:
    case kFidlCodedPrimitiveSubtype_Float32:
      return 4;
    case kFidlCodedPrimitiveSubtype_Int64:
    case kFidlCodedPrimitiveSubtype_Uint64:
    case kFidlCodedPrimitiveSubtype_Float64:
      return 8;
  }
  __builtin_unreachable();
}

constexpr uint32_t TypeSizeV1(const fidl_type_t* type) {
  switch (type->type_tag()) {
    case kFidlTypePrimitive:
      return PrimitiveSize(type->coded_primitive().type);
    case kFidlTypeEnum:
      return PrimitiveSize(type->coded_enum().underlying_type);
    case kFidlTypeBits:
      return PrimitiveSize(type->coded_bits().underlying_type);
    case kFidlTypeStructPointer:
      return sizeof(uint64_t);
    case kFidlTypeHandle:
      return sizeof(zx_handle_t);
    case kFidlTypeStruct:
      return type->coded_struct().size_v1;
    case kFidlTypeTable:
      return sizeof(fidl_vector_t);
    case kFidlTypeXUnion:
      return 24;
    case kFidlTypeString:
      return sizeof(fidl_string_t);
    case kFidlTypeArray:
      return type->coded_array().array_size_v1;
    case kFidlTypeVector:
      return sizeof(fidl_vector_t);
  }
  __builtin_unreachable();
}

constexpr uint32_t TypeSizeV2(const fidl_type_t* type) {
  switch (type->type_tag()) {
    case kFidlTypePrimitive:
      return PrimitiveSize(type->coded_primitive().type);
    case kFidlTypeEnum:
      return PrimitiveSize(type->coded_enum().underlying_type);
    case kFidlTypeBits:
      return PrimitiveSize(type->coded_bits().underlying_type);
    case kFidlTypeStructPointer:
      return sizeof(uint64_t);
    case kFidlTypeHandle:
      return sizeof(zx_handle_t);
    case kFidlTypeStruct:
      return type->coded_struct().size_v2;
    case kFidlTypeTable:
      return sizeof(fidl_vector_t);
    case kFidlTypeXUnion:
      return 16;
    case kFidlTypeString:
      return sizeof(fidl_string_t);
    case kFidlTypeArray:
      return type->coded_array().array_size_v2;
    case kFidlTypeVector:
      return sizeof(fidl_vector_t);
  }
  __builtin_unreachable();
}

// Transformer that converts between the V1 and V2 wire formats (and vice versa).
//
// The invariant held by Transform* methods is that each Transform* method is responsible for
// writing "itself" to the output. That is, TransformVector will ensure the vector header
// and body is converted to the appropriate wire format and copied.
class Transformer {
 public:
  Transformer(fidl_transformation_t transformation, const uint8_t* src_bytes,
              uint32_t src_num_bytes, uint8_t* dst_bytes, uint32_t dst_num_bytes_capacity)
      : transformation_(transformation),
        src_bytes_(src_bytes),
        dst_bytes_(dst_bytes),
        src_next_out_of_line_(0),
        dst_next_out_of_line_(0),
        src_num_bytes_(src_num_bytes),
        dst_num_bytes_capacity_(dst_num_bytes_capacity),
        error(nullptr) {
    ZX_DEBUG_ASSERT(transformation_ == FIDL_TRANSFORMATION_V1_TO_V2 ||
                    transformation_ == FIDL_TRANSFORMATION_V2_TO_V1);
    ZX_DEBUG_ASSERT(FidlIsAligned(src_bytes));
    ZX_DEBUG_ASSERT(FidlIsAligned(dst_bytes));
  }

  // Performs a transform inside of a primary / new out of line object.
  zx_status_t Transform(const fidl_type_t* type, uint32_t src_offset, uint32_t dst_offset) {
    zx_status_t status =
        IncreaseNextOutOfLineV1V2(FIDL_ALIGN(TypeSizeV1(type)), FIDL_ALIGN(TypeSizeV2(type)));
    if (status != ZX_OK) {
      return status;
    }
    return TransformInline(type, src_offset, dst_offset);
  }

  const char* err_msg() { return error; }
  uint32_t dst_num_bytes() { return dst_next_out_of_line_; }

 private:
  // Performs a transform inside of an inline object.
  zx_status_t TransformInline(const fidl_type_t* type, uint32_t src_offset, uint32_t dst_offset) {
    switch (type->type_tag()) {
      case kFidlTypePrimitive:
        return TransformPrimitive(&type->coded_primitive(), src_offset, dst_offset);
      case kFidlTypeEnum:
        return TransformEnum(&type->coded_enum(), src_offset, dst_offset);
      case kFidlTypeBits:
        return TransformBits(&type->coded_bits(), src_offset, dst_offset);
      case kFidlTypeHandle:
        return TransformHandle(&type->coded_handle(), src_offset, dst_offset);
      case kFidlTypeStruct:
        return TransformStruct(&type->coded_struct(), src_offset, dst_offset);
      case kFidlTypeArray:
        return TransformArray(&type->coded_array(), src_offset, dst_offset);
      case kFidlTypeStructPointer:
        return TransformStructPointer(&type->coded_struct_pointer(), src_offset, dst_offset);
      case kFidlTypeString:
        return TransformString(&type->coded_string(), src_offset, dst_offset);
      case kFidlTypeVector:
        return TransformVector(&type->coded_vector(), src_offset, dst_offset);
      case kFidlTypeXUnion:
        return TransformXUnion(&type->coded_xunion(), src_offset, dst_offset);
      case kFidlTypeTable:
        return TransformTable(&type->coded_table(), src_offset, dst_offset);
    }
    __builtin_unreachable();
  }

  zx_status_t TransformPrimitive(const FidlCodedPrimitive* coded_primitive, uint32_t src_offset,
                                 uint32_t dst_offset) {
    uint32_t size = PrimitiveSize(coded_primitive->type);

    ZX_DEBUG_ASSERT(src_offset + size <= src_next_out_of_line_);
    ZX_DEBUG_ASSERT(src_next_out_of_line_ <= src_num_bytes_);
    ZX_DEBUG_ASSERT(dst_offset + size <= dst_next_out_of_line_);
    ZX_DEBUG_ASSERT(dst_next_out_of_line_ <= dst_num_bytes_capacity_);

    memcpy(&dst_bytes_[dst_offset], &src_bytes_[src_offset], size);

    return ZX_OK;
  }

  zx_status_t TransformEnum(const FidlCodedEnum* coded_enum, uint32_t src_offset,
                            uint32_t dst_offset) {
    uint32_t size = PrimitiveSize(coded_enum->underlying_type);

    ZX_DEBUG_ASSERT(src_offset + size <= src_next_out_of_line_);
    ZX_DEBUG_ASSERT(src_next_out_of_line_ <= src_num_bytes_);
    ZX_DEBUG_ASSERT(dst_offset + size <= dst_next_out_of_line_);
    ZX_DEBUG_ASSERT(dst_next_out_of_line_ <= dst_num_bytes_capacity_);

    memcpy(&dst_bytes_[dst_offset], &src_bytes_[src_offset], size);

    return ZX_OK;
  }

  zx_status_t TransformBits(const FidlCodedBits* coded_bits, uint32_t src_offset,
                            uint32_t dst_offset) {
    uint32_t size = PrimitiveSize(coded_bits->underlying_type);

    ZX_DEBUG_ASSERT(src_offset + size <= src_next_out_of_line_);
    ZX_DEBUG_ASSERT(src_next_out_of_line_ <= src_num_bytes_);
    ZX_DEBUG_ASSERT(dst_offset + size <= dst_next_out_of_line_);
    ZX_DEBUG_ASSERT(dst_next_out_of_line_ <= dst_num_bytes_capacity_);

    memcpy(&dst_bytes_[dst_offset], &src_bytes_[src_offset], size);

    return ZX_OK;
  }

  zx_status_t TransformHandle(const FidlCodedHandle* coded_handle, uint32_t src_offset,
                              uint32_t dst_offset) {
    ZX_DEBUG_ASSERT(src_offset + sizeof(zx_handle_t) <= src_next_out_of_line_);
    ZX_DEBUG_ASSERT(src_next_out_of_line_ <= src_num_bytes_);
    ZX_DEBUG_ASSERT(dst_offset + sizeof(zx_handle_t) <= dst_next_out_of_line_);
    ZX_DEBUG_ASSERT(dst_next_out_of_line_ <= dst_num_bytes_capacity_);

    memcpy(&dst_bytes_[dst_offset], &src_bytes_[src_offset], sizeof(zx_handle_t));

    return ZX_OK;
  }

  zx_status_t TransformStruct(const FidlCodedStruct* coded_struct, uint32_t src_offset,
                              uint32_t dst_offset) {
    ZX_DEBUG_ASSERT(src_offset + SRC_VALUE(coded_struct->size_v1, coded_struct->size_v2) <=
                    src_next_out_of_line_);
    ZX_DEBUG_ASSERT(src_next_out_of_line_ <= src_num_bytes_);
    ZX_DEBUG_ASSERT(dst_offset + DST_VALUE(coded_struct->size_v1, coded_struct->size_v2) <=
                    dst_next_out_of_line_);
    ZX_DEBUG_ASSERT(dst_next_out_of_line_ <= dst_num_bytes_capacity_);

    // 1. Copy up to the next non-padding field.
    // 2. Call the inner transformer for that field.
    // 3. Repeat 1 & 2 as needed.
    // 4. Copy up to the end of the struct.

    uint32_t inner_src_offset = 0;
    uint32_t inner_dst_offset = 0;
    for (uint32_t i = 0; i < coded_struct->element_count; i++) {
      const struct FidlStructElement& element = coded_struct->elements[i];
      switch (element.header.element_type) {
        case kFidlStructElementType_Field: {
          uint32_t new_inner_src_offset =
              SRC_VALUE(element.field.offset_v1, element.field.offset_v2);
          uint32_t new_inner_dst_offset =
              DST_VALUE(element.field.offset_v1, element.field.offset_v2);

          ZX_DEBUG_ASSERT(new_inner_src_offset - inner_src_offset ==
                          new_inner_dst_offset - inner_dst_offset);
          if (new_inner_src_offset > inner_src_offset) {
            memcpy(&dst_bytes_[dst_offset + inner_dst_offset],
                   &src_bytes_[src_offset + inner_src_offset],
                   new_inner_src_offset - inner_src_offset);
          }

          zx_status_t status =
              TransformInline(element.field.field_type, src_offset + new_inner_src_offset,
                              dst_offset + new_inner_dst_offset);
          if (status != ZX_OK) {
            return status;
          }

          inner_src_offset = new_inner_src_offset + SRC_VALUE(TypeSizeV1(element.field.field_type),
                                                              TypeSizeV2(element.field.field_type));
          inner_dst_offset = new_inner_dst_offset + DST_VALUE(TypeSizeV1(element.field.field_type),
                                                              TypeSizeV2(element.field.field_type));
        } break;
        case kFidlStructElementType_Padding64:
          break;
        case kFidlStructElementType_Padding32:
          break;
        case kFidlStructElementType_Padding16:
          break;
      }
    }

    uint32_t src_size = SRC_VALUE(coded_struct->size_v1, coded_struct->size_v2);
    uint32_t dst_size = DST_VALUE(coded_struct->size_v1, coded_struct->size_v2);
    ZX_DEBUG_ASSERT(src_size - inner_src_offset == dst_size - inner_dst_offset);
    if (src_size > inner_src_offset) {
      memcpy(&dst_bytes_[dst_offset + inner_dst_offset], &src_bytes_[src_offset + inner_src_offset],
             src_size - inner_src_offset);
    }
    return ZX_OK;
  }

  zx_status_t TransformArray(const FidlCodedArray* coded_array, uint32_t src_offset,
                             uint32_t dst_offset) {
    uint32_t src_element_size =
        SRC_VALUE(coded_array->element_size_v1, coded_array->element_size_v2);
    uint32_t dst_element_size =
        DST_VALUE(coded_array->element_size_v1, coded_array->element_size_v2);
    uint32_t src_offset_end =
        src_offset + SRC_VALUE(coded_array->array_size_v1, coded_array->array_size_v2);

    for (; src_offset < src_offset_end;
         src_offset += src_element_size, dst_offset += dst_element_size) {
      zx_status_t status = TransformInline(coded_array->element, src_offset, dst_offset);
      if (status != ZX_OK) {
        return status;
      }
    }
    return ZX_OK;
  }

  zx_status_t TransformStructPointer(const FidlCodedStructPointer* coded_struct_pointer,
                                     uint32_t src_offset, uint32_t dst_offset) {
    switch (src_u64(src_offset)) {
      case FIDL_ALLOC_ABSENT:
        dst_u64(dst_offset) = FIDL_ALLOC_ABSENT;
        return ZX_OK;
      case FIDL_ALLOC_PRESENT: {
        dst_u64(dst_offset) = FIDL_ALLOC_PRESENT;

        uint32_t src_next_offset = src_next_out_of_line_;
        uint32_t dst_next_offset = dst_next_out_of_line_;

        zx_status_t status =
            IncreaseNextOutOfLineV1V2(FIDL_ALIGN(coded_struct_pointer->struct_type->size_v1),
                                      FIDL_ALIGN(coded_struct_pointer->struct_type->size_v2));
        if (status != ZX_OK) {
          return status;
        }

        return TransformStruct(coded_struct_pointer->struct_type, src_next_offset, dst_next_offset);
      }
      default:
        return ZX_ERR_INVALID_ARGS;
    }
  }

  zx_status_t TransformString(const FidlCodedString* coded_string, uint32_t src_offset,
                              uint32_t dst_offset) {
    // Copy count.
    uint64_t count = src_u64(src_offset);
    dst_u64(dst_offset) = count;

    // Copy presence marker.
    if (count > 0 && src_u64(src_offset + 8) != FIDL_ALLOC_PRESENT) {
      error = "expected present marker on non-empty string";
      return ZX_ERR_INVALID_ARGS;
    }
    if (count > std::numeric_limits<uint32_t>::max()) {
      error = "count exceeds the maximum value that fits in a uint32_t";
      return ZX_ERR_INVALID_ARGS;
    }
    dst_u64(dst_offset + 8) = src_u64(src_offset + 8);

    // Copy body.
    uint32_t src_body_offset = src_next_out_of_line_;
    uint32_t dst_body_offset = dst_next_out_of_line_;
    zx_status_t status = IncreaseNextOutOfLine(FIDL_ALIGN(count), FIDL_ALIGN(count));
    if (status != ZX_OK) {
      return status;
    }
    memcpy(&dst_bytes_[dst_body_offset], &src_bytes_[src_body_offset], count);
    return ZX_OK;
  }

  zx_status_t TransformVector(const FidlCodedVector* coded_vector, uint32_t src_offset,
                              uint32_t dst_offset) {
    // Copy count.
    uint64_t count = src_u64(src_offset);
    dst_u64(dst_offset) = count;

    // Copy presence marker.
    if (count > 0 && src_u64(src_offset + 8) != FIDL_ALLOC_PRESENT) {
      error = "expected present marker on non-empty vector";
      return ZX_ERR_INVALID_ARGS;
    }
    if (count > std::numeric_limits<uint32_t>::max()) {
      error = "count exceeds the maximum value that fits in a uint32_t";
      return ZX_ERR_INVALID_ARGS;
    }
    dst_u64(dst_offset + 8) = src_u64(src_offset + 8);

    uint32_t src_element_size =
        SRC_VALUE(coded_vector->element_size_v1, coded_vector->element_size_v2);
    uint32_t dst_element_size =
        DST_VALUE(coded_vector->element_size_v1, coded_vector->element_size_v2);

    uint32_t src_body_offset = src_next_out_of_line_;
    uint32_t dst_body_offset = dst_next_out_of_line_;

    uint32_t src_size;
    if (mul_overflow(count, src_element_size, &src_size)) {
      error = "exceeded src array size";
      return ZX_ERR_INVALID_ARGS;
    }
    uint32_t dst_size;
    if (mul_overflow(count, dst_element_size, &dst_size)) {
      error = "exceeded dst array size";
      return ZX_ERR_INVALID_ARGS;
    }

    zx_status_t status = IncreaseNextOutOfLine(FIDL_ALIGN(src_size), FIDL_ALIGN(dst_size));
    if (status != ZX_OK) {
      return status;
    }
    for (uint32_t i = 0; i < count; i++) {
      status = TransformInline(coded_vector->element, src_body_offset, dst_body_offset);
      if (status != ZX_OK) {
        return status;
      }
      src_body_offset += src_element_size;
      dst_body_offset += dst_element_size;
    }
    return ZX_OK;
  }

  zx_status_t TransformEnvelopeV1ToV2(const fidl_type_t* type, uint32_t src_offset,
                                      uint32_t dst_offset) {
    WireEnvelopeV1 src_envelope = src<WireEnvelopeV1>(src_offset);

    switch (src_envelope.presence_marker) {
      case FIDL_ALLOC_PRESENT:
        if (src_envelope.num_bytes == 0) {
          error = "envelope is present but num_bytes is 0";
          return ZX_ERR_INVALID_ARGS;
        }
        break;
      case FIDL_ALLOC_ABSENT:
        if (src_envelope.num_bytes != 0) {
          error = "envelope is absent but num_bytes is not 0";
          return ZX_ERR_INVALID_ARGS;
        }
        return ZX_OK;
      default:
        error = "invalid presence marker";
        return ZX_ERR_INVALID_ARGS;
    }
    if (src_envelope.num_handles > std::numeric_limits<uint16_t>::max()) {
      error = "num_handles exceeds the maximum value that fits in a uint16_t";
      return ZX_ERR_INVALID_ARGS;
    }

    if (type == nullptr) {
      // Unknown value.
      if (src_envelope.num_bytes % 8 != 0) {
        error = "unknown value contained non 8-byte aligned payload";
        return ZX_ERR_INVALID_ARGS;
      }

      dst<WireEnvelopeV2>(dst_offset) = WireEnvelopeV2{
          .num_bytes = src_envelope.num_bytes,
          .num_handles = static_cast<uint16_t>(src_envelope.num_handles),
          .flags = kEmptyFlags,
      };

      uint32_t src_block_offset = src_next_out_of_line_;
      uint32_t dst_block_offset = dst_next_out_of_line_;
      zx_status_t status = IncreaseNextOutOfLine(src_envelope.num_bytes, src_envelope.num_bytes);
      if (status != ZX_OK) {
        return status;
      }

      memcpy(&dst_bytes_[dst_block_offset], &src_bytes_[src_block_offset], src_envelope.num_bytes);
      return ZX_OK;
    }

    if (TypeSizeV2(type) <= 4) {
      // Inlined value.
      WireEnvelopeV2 dst_envelope{
          .num_handles = static_cast<uint16_t>(src_envelope.num_handles),
          .flags = kInlinedEnvelopeFlag,
      };

      uint32_t src_block_offset = src_next_out_of_line_;
      zx_status_t status = IncreaseNextOutOfLine(8, 0);
      if (status != ZX_OK) {
        return status;
      }

      memcpy(dst_envelope.inlined_value, &src_bytes_[src_block_offset],
             sizeof(dst_envelope.inlined_value));
      dst<WireEnvelopeV2>(dst_offset) = dst_envelope;
      return ZX_OK;
    }

    uint32_t checkpoint_dst_next_out_of_line = dst_next_out_of_line_;
    zx_status_t status = Transform(type, src_next_out_of_line_, dst_next_out_of_line_);
    if (status != ZX_OK) {
      return status;
    }
    dst<WireEnvelopeV2>(dst_offset) = WireEnvelopeV2{
        .num_bytes = dst_next_out_of_line_ - checkpoint_dst_next_out_of_line,
        .num_handles = static_cast<uint16_t>(src_envelope.num_handles),
        .flags = kEmptyFlags,
    };
    return ZX_OK;
  }

  zx_status_t TransformEnvelopeV2ToV1(const fidl_type_t* type, uint32_t src_offset,
                                      uint32_t dst_offset) {
    WireEnvelopeV2 src_envelope = src<WireEnvelopeV2>(src_offset);

    if (!ValidFlags(src_envelope)) {
      error = "invalid inline marker";
      return ZX_ERR_INVALID_ARGS;
    }

    if (IsInlined(src_envelope)) {
      dst<WireEnvelopeV1>(dst_offset) = WireEnvelopeV1{
          .num_bytes = 8,
          .num_handles = src_envelope.num_handles,
          .presence_marker = FIDL_ALLOC_PRESENT,
      };

      uint32_t dst_block_offset = dst_next_out_of_line_;
      zx_status_t status = IncreaseNextOutOfLine(0, 8);
      if (status != ZX_OK) {
        return status;
      }

      memcpy(&dst_bytes_[dst_block_offset], src_envelope.inlined_value,
             sizeof(src_envelope.inlined_value));
      memset(&dst_bytes_[dst_block_offset + 4], 0, 4);
      return ZX_OK;
    }

    if (src_envelope.num_bytes > 0 || src_envelope.num_handles > 0) {
      if (type != nullptr) {
        uint32_t checkpoint_dst_next_out_of_line = dst_next_out_of_line_;
        zx_status_t status = Transform(type, src_next_out_of_line_, dst_next_out_of_line_);
        if (status != ZX_OK) {
          return status;
        }

        dst<WireEnvelopeV1>(dst_offset) = WireEnvelopeV1{
            .num_bytes = dst_next_out_of_line_ - checkpoint_dst_next_out_of_line,
            .num_handles = src_envelope.num_handles,
            .presence_marker = FIDL_ALLOC_PRESENT,
        };
        return ZX_OK;
      }

      // Unknown value.
      if (src_envelope.num_bytes % 8 != 0) {
        error = "unknown value contained non 8-byte aligned payload";
        return ZX_ERR_INVALID_ARGS;
      }

      dst<WireEnvelopeV1>(dst_offset) = WireEnvelopeV1{
          .num_bytes = src_envelope.num_bytes,
          .num_handles = src_envelope.num_handles,
          .presence_marker = FIDL_ALLOC_PRESENT,
      };

      uint32_t src_block_offset = src_next_out_of_line_;
      uint32_t dst_block_offset = dst_next_out_of_line_;
      zx_status_t status = IncreaseNextOutOfLine(src_envelope.num_bytes, src_envelope.num_bytes);
      if (status != ZX_OK) {
        return status;
      }

      memcpy(&dst_bytes_[dst_block_offset], &src_bytes_[src_block_offset], src_envelope.num_bytes);
      return ZX_OK;
    }

    dst<WireEnvelopeV1>(dst_offset) = WireEnvelopeV1{
        .num_bytes = 0,
        .num_handles = src_envelope.num_handles,
        .presence_marker = FIDL_ALLOC_ABSENT,
    };
    return ZX_OK;
  }

  zx_status_t TransformEnvelope(const fidl_type_t* type, uint32_t src_offset, uint32_t dst_offset) {
    if (transformation_ == FIDL_TRANSFORMATION_V2_TO_V1) {
      return TransformEnvelopeV2ToV1(type, src_offset, dst_offset);
    } else {
      return TransformEnvelopeV1ToV2(type, src_offset, dst_offset);
    }
  }

  zx_status_t TransformXUnion(const FidlCodedXUnion* coded_xunion, uint32_t src_offset,
                              uint32_t dst_offset) {
    uint64_t ordinal = src_u64(src_offset);
    dst_u64(dst_offset) = ordinal;
    const fidl_type_t* field_type = nullptr;
    if (ordinal > 0 && ordinal <= coded_xunion->field_count) {
      field_type = coded_xunion->fields[ordinal - 1].type;
    }
    return TransformEnvelope(field_type, src_offset + sizeof(ordinal),
                             dst_offset + sizeof(ordinal));
  }

  zx_status_t TransformTable(const FidlCodedTable* coded_table, uint32_t src_offset,
                             uint32_t dst_offset) {
    const WireTableHeader& src_table_header =
        *reinterpret_cast<const WireTableHeader*>(&src_bytes_[src_offset]);
    WireTableHeader& dst_table_header =
        *reinterpret_cast<WireTableHeader*>(&dst_bytes_[dst_offset]);

    if (src_table_header.count > std::numeric_limits<uint32_t>::max()) {
      error = "count exceeds the maximum value that fits in a uint32_t";
      return ZX_ERR_INVALID_ARGS;
    }
    if (src_table_header.count > 0 && src_table_header.presence_marker != FIDL_ALLOC_PRESENT) {
      error = "expected present marker on non-empty table";
      return ZX_ERR_INVALID_ARGS;
    }

    dst_table_header.count = src_table_header.count;
    dst_table_header.presence_marker = src_table_header.presence_marker;

    uint32_t src_element_size = SRC_VALUE(16, 8);
    uint32_t dst_element_size = DST_VALUE(16, 8);
    uint32_t src_body_offset = src_next_out_of_line_;
    uint32_t dst_body_offset = dst_next_out_of_line_;
    zx_status_t status =
        IncreaseNextOutOfLine(static_cast<uint32_t>(src_table_header.count) * src_element_size,
                              static_cast<uint32_t>(src_table_header.count) * dst_element_size);
    if (status != ZX_OK) {
      return status;
    }

    // Process the table body.
    uint32_t f = 0;
    for (uint32_t i = 0; i < src_table_header.count; i++) {
      const fidl_type_t* field_type = nullptr;
      if (f < coded_table->field_count && i + 1 == coded_table->fields[f].ordinal) {
        field_type = coded_table->fields[f].type;
        f++;
      }
      status = TransformEnvelope(field_type, src_body_offset, dst_body_offset);
      if (status != ZX_OK) {
        return status;
      }
      src_body_offset += src_element_size;
      dst_body_offset += dst_element_size;
    }
    return ZX_OK;
  }

  zx_status_t IncreaseNextOutOfLineV1V2(uint32_t v1_size, uint32_t v2_size) {
    return IncreaseNextOutOfLine(SRC_VALUE(v1_size, v2_size), DST_VALUE(v1_size, v2_size));
  }

  zx_status_t IncreaseNextOutOfLine(uint32_t src_size, uint32_t dst_size) {
    ZX_DEBUG_ASSERT(src_size % 8 == 0);
    ZX_DEBUG_ASSERT(dst_size % 8 == 0);

    uint32_t new_src_next_out_of_line;
    if (add_overflow(src_next_out_of_line_, src_size, &new_src_next_out_of_line)) {
      error = "overflow in src_next_out_of_line";
      return ZX_ERR_INVALID_ARGS;
    }
    if (new_src_next_out_of_line > src_num_bytes_) {
      error = "exceeded src array size";
      return ZX_ERR_INVALID_ARGS;
    }
    src_next_out_of_line_ = new_src_next_out_of_line;

    uint32_t new_dst_next_out_of_line;
    if (add_overflow(dst_next_out_of_line_, dst_size, &new_dst_next_out_of_line)) {
      error = "overflow in dst_next_out_of_line";
      return ZX_ERR_INVALID_ARGS;
    }
    if (new_dst_next_out_of_line > dst_num_bytes_capacity_) {
      error = "exceeded dst array size";
      return ZX_ERR_INVALID_ARGS;
    }
    dst_next_out_of_line_ = new_dst_next_out_of_line;

    return ZX_OK;
  }

  template <typename T>
  T src(uint32_t offset) {
    ZX_DEBUG_ASSERT(offset + sizeof(T) <= src_next_out_of_line_);
    ZX_DEBUG_ASSERT(src_next_out_of_line_ <= src_num_bytes_);
    ZX_DEBUG_ASSERT(offset % 8 == 0);
    // Use memcpy rather than reinterpret_cast to avoid issues
    // due to the strict aliasing rule.
    T value;
    memcpy(&value, &src_bytes_[offset], sizeof(T));
    return value;
  }

  uint64_t src_u64(uint32_t offset) { return src<uint64_t>(offset); }

  // Target to facillitate assignment with dst() and dst_u64().
  // e.g. dst(offset) = value;
  template <typename T>
  class DstTarget {
   public:
    DstTarget(Transformer* transformer, uint32_t offset)
        : transformer_(transformer), offset_(offset) {}

    DstTarget& operator=(const T& value) {
      // Use memcpy rather than reinterpret_cast to avoid issues
      // due to the strict aliasing rule.
      memcpy(&transformer_->dst_bytes_[offset_], &value, sizeof(T));
      return *this;
    }

   private:
    Transformer* transformer_;
    uint32_t offset_;
  };

  template <typename T>
  DstTarget<T> dst(uint32_t offset) {
    ZX_DEBUG_ASSERT(offset + sizeof(T) <= dst_next_out_of_line_);
    ZX_DEBUG_ASSERT(dst_next_out_of_line_ <= dst_num_bytes_capacity_);
    ZX_DEBUG_ASSERT(offset % 8 == 0);
    return DstTarget<T>(this, offset);
  }

  DstTarget<uint64_t> dst_u64(uint32_t offset) { return dst<uint64_t>(offset); }

  fidl_transformation_t transformation_;

  const uint8_t* src_bytes_;
  uint8_t* dst_bytes_;

  uint32_t src_next_out_of_line_;
  uint32_t dst_next_out_of_line_;

  uint32_t src_num_bytes_;
  uint32_t dst_num_bytes_capacity_;

  const char* error;
};

}  // namespace

zx_status_t internal__fidl_transform__may_break(fidl_transformation_t transformation,
                                                const fidl_type_t* type, const uint8_t* src_bytes,
                                                uint32_t src_num_bytes, uint8_t* dst_bytes,
                                                uint32_t dst_num_bytes_capacity,
                                                uint32_t* out_dst_num_bytes,
                                                const char** out_error_msg) {
  if (transformation == FIDL_TRANSFORMATION_NONE) {
    // Fast path - directly copy if no transformation needs to be performed.
    if (dst_num_bytes_capacity < src_num_bytes) {
      *out_error_msg = "destination capacity too small";
      return ZX_ERR_INVALID_ARGS;
    }
    memcpy(dst_bytes, src_bytes, src_num_bytes);
    *out_dst_num_bytes = src_num_bytes;
    return ZX_OK;
  }

  memset(dst_bytes, 0, dst_num_bytes_capacity);
  Transformer transformer(transformation, src_bytes, src_num_bytes, dst_bytes,
                          dst_num_bytes_capacity);
  zx_status_t status = transformer.Transform(type, 0, 0);
  *out_error_msg = transformer.err_msg();
  *out_dst_num_bytes = transformer.dst_num_bytes();
  return status;
}
