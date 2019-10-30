// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/internal.h>
#include <lib/fidl/transformer.h>

#include <cstring>

namespace {

enum class WireFormat {
  kOld,
  kV1,
};

uint32_t AlignedInlineSize(const fidl_type_t* type, WireFormat wire_format) {
  if (!type) {
    // For integral types (i.e. primitive, enum, bits).
    return 8;
  }
  switch (type->type_tag) {
    case fidl::kFidlTypePrimitive:
    case fidl::kFidlTypeEnum:
    case fidl::kFidlTypeBits:
      assert(false && "bug: should not get called");
      return 0;
    case fidl::kFidlTypeStructPointer:
    case fidl::kFidlTypeUnionPointer:
      return 8;
    case fidl::kFidlTypeVector:
    case fidl::kFidlTypeString:
      return 16;
    case fidl::kFidlTypeStruct:
      return type->coded_struct.size;
    case fidl::kFidlTypeUnion:
      switch (wire_format) {
        case WireFormat::kOld:
          return type->coded_union.size;
        case WireFormat::kV1:
          return 24;  // xunion
        default:
          assert(false && "bug: unreachable");
          return 0;
      }
    case fidl::kFidlTypeArray:
      return type->coded_array.array_size;
    case fidl::kFidlTypeHandle:
    case fidl::kFidlTypeTable:
    case fidl::kFidlTypeXUnion:
    default:
      assert(false && "TODO!");
      return 0;
  }
}

struct Position {
  uint32_t src_inline_offset = 0;
  uint32_t src_out_of_line_offset = 0;
  uint32_t dst_inline_offset = 0;
  uint32_t dst_out_of_line_offset = 0;

  inline Position IncreaseInlineOffset(uint32_t increase) const {
    return IncreaseSrcInlineOffset(increase).IncreaseDstInlineOffset(increase);
  }

  inline Position IncreaseSrcInlineOffset(uint32_t increase) const {
    return Position{
        .src_inline_offset = src_inline_offset + increase,
        .src_out_of_line_offset = src_out_of_line_offset,
        .dst_inline_offset = dst_inline_offset,
        .dst_out_of_line_offset = dst_out_of_line_offset,
    };
  }

  inline Position IncreaseDstInlineOffset(uint32_t increase) const {
    return Position{
        .src_inline_offset = src_inline_offset,
        .src_out_of_line_offset = src_out_of_line_offset,
        .dst_inline_offset = dst_inline_offset + increase,
        .dst_out_of_line_offset = dst_out_of_line_offset,
    };
  }
};

class SrcDst final {
 public:
  SrcDst(const uint8_t* src_bytes, const uint32_t src_num_bytes, uint8_t* dst_bytes,
         uint32_t* out_dst_num_bytes)
      : src_bytes_(src_bytes),
        src_num_bytes_(src_num_bytes),
        dst_bytes_(dst_bytes),
        out_dst_num_bytes_(out_dst_num_bytes) {}
  SrcDst(const SrcDst&) = delete;

  ~SrcDst() { *out_dst_num_bytes_ = dst_max_offset_; }

  template <typename T>  // TODO: restrict T should be pointer type
  const T* Read(const Position& position) const {
    uint32_t size = sizeof(T);
    if (!(position.src_inline_offset + size <= src_num_bytes_)) {
      return nullptr;
    }

    return reinterpret_cast<const T*>(src_bytes_ + position.src_inline_offset);
  }

  bool Copy(const Position& position, uint32_t size) {
    if (!(position.src_inline_offset + size <= src_num_bytes_)) {
      return false;
    }

    memcpy(dst_bytes_ + position.dst_inline_offset, src_bytes_ + position.src_inline_offset, size);
    UpdateMaxOffset(position.dst_inline_offset + size);
    return true;
  }

  void Pad(const Position& position, uint32_t size) {
    memset(dst_bytes_ + position.dst_inline_offset, 0, size);
    UpdateMaxOffset(position.dst_inline_offset + size);
  }

  template <typename T>
  void Write(const Position& position, T value) {
    auto ptr = reinterpret_cast<T*>(dst_bytes_ + position.dst_inline_offset);
    *ptr = value;
    uint32_t size = sizeof(T);
    UpdateMaxOffset(position.dst_inline_offset + size);
  }

 private:
  void UpdateMaxOffset(uint32_t dst_offset) {
    if (dst_offset > dst_max_offset_) {
      dst_max_offset_ = dst_offset;
    }
  }

  const uint8_t* src_bytes_;
  const uint32_t src_num_bytes_;
  uint8_t* dst_bytes_;
  uint32_t* out_dst_num_bytes_;

  uint32_t dst_max_offset_ = 0;
};

class TransformerBase {
 public:
  TransformerBase(SrcDst* src_dst, const char** out_error_msg)
      : src_dst(src_dst), out_error_msg_(out_error_msg) {}
  virtual ~TransformerBase() = default;

  zx_status_t TransformTopLevelStruct(const fidl_type_t* type) {
    if (type->type_tag != fidl::kFidlTypeStruct) {
      return Fail(ZX_ERR_INVALID_ARGS, "only top-level structs supported");
    }

    const auto& src_coded_struct = type->coded_struct;
    const auto& dst_coded_struct = *src_coded_struct.alt_type;
    // Since this is the top-level struct, the first secondary object (i.e.
    // out-of-line offset) is exactly placed after this struct, i.e. the
    // struct's inline size.
    const auto start_position = Position{
        .src_inline_offset = 0,
        .src_out_of_line_offset = src_coded_struct.size,
        .dst_inline_offset = 0,
        .dst_out_of_line_offset = dst_coded_struct.size,
    };
    return TransformStruct(src_coded_struct, dst_coded_struct,
                           start_position, dst_coded_struct.size);
  }

 protected:
  zx_status_t Transform(const fidl_type_t* type, const Position& position,
                        const uint32_t dst_size) {
    if (!type) {
      goto no_transform_just_copy;
    }

    switch (type->type_tag) {
      case fidl::kFidlTypePrimitive:
      case fidl::kFidlTypeEnum:
      case fidl::kFidlTypeBits:
      case fidl::kFidlTypeHandle:
        goto no_transform_just_copy;
      case fidl::kFidlTypeStructPointer: {
        const auto& src_coded_struct = *type->coded_struct_pointer.struct_type;
        const auto& dst_coded_struct = *src_coded_struct.alt_type;
        return TransformStructPointer(src_coded_struct, dst_coded_struct, position);
      }
      case fidl::kFidlTypeUnionPointer:
        assert(false && "nullable unions are no longer supported");
        return ZX_ERR_BAD_STATE;
      case fidl::kFidlTypeStruct: {
        const auto& src_coded_struct = type->coded_struct;
        const auto& dst_coded_struct = *src_coded_struct.alt_type;
        return TransformStruct(src_coded_struct, dst_coded_struct, position, dst_size);
      }
      case fidl::kFidlTypeUnion: {
        const auto& src_coded_union = type->coded_union;
        const auto& dst_coded_union = *src_coded_union.alt_type;
        return TransformUnion(src_coded_union, dst_coded_union, position);
      }
      case fidl::kFidlTypeArray: {
        const auto convert = [](const fidl::FidlCodedArray& coded_array) {
          return fidl::FidlCodedArrayNew(
              coded_array.element, coded_array.array_size / coded_array.element_size,
              coded_array.element_size, 0,
              nullptr /* alt_type unused, we provide both src and dst */);
        };
        auto src_coded_array = convert(type->coded_array);
        auto dst_coded_array = convert(*type->coded_array.alt_type);
        uint32_t dst_array_size = type->coded_array.alt_type->array_size;
        return TransformArray(src_coded_array, dst_coded_array, position, dst_array_size);
      }
      case fidl::kFidlTypeString:
        return TransformString(position);
      case fidl::kFidlTypeVector: {
        const auto& src_coded_vector = type->coded_vector;
        const auto& dst_coded_vector = *src_coded_vector.alt_type;
        return TransformVector(src_coded_vector, dst_coded_vector, position);
      }
      case fidl::kFidlTypeTable:
      case fidl::kFidlTypeXUnion:
        assert(false && "TODO(apang)");
        return ZX_ERR_BAD_STATE;
      default:
        return ZX_ERR_BAD_STATE;
    }

  no_transform_just_copy:
    if (!src_dst->Copy(position, dst_size)) {
      return ZX_ERR_BUFFER_TOO_SMALL;
    }
    return ZX_OK;
  }

  virtual zx_status_t TransformStructPointer(const fidl::FidlCodedStruct& src_coded_struct,
                                             const fidl::FidlCodedStruct& dst_coded_struct,
                                             const Position& position) = 0;

  virtual zx_status_t TransformStruct(const fidl::FidlCodedStruct& src_coded_struct,
                                      const fidl::FidlCodedStruct& dst_coded_struct,
                                      const Position& position, uint32_t dst_size) = 0;

  virtual zx_status_t TransformUnion(const fidl::FidlCodedUnion& src_coded_union,
                                     const fidl::FidlCodedUnion& dst_coded_union,
                                     const Position& position) = 0;

  virtual zx_status_t TransformString(const Position& position) = 0;

  virtual zx_status_t TransformVector(const fidl::FidlCodedVector& src_coded_vector,
                                      const fidl::FidlCodedVector& dst_coded_vector,
                                      const Position& position) = 0;

  virtual zx_status_t TransformArray(const fidl::FidlCodedArrayNew& src_coded_array,
                                     const fidl::FidlCodedArrayNew& dst_coded_array,
                                     const Position& position, uint32_t dst_array_size) = 0;

  inline zx_status_t Fail(zx_status_t status, const char* error_msg) {
    if (out_error_msg_)
      *out_error_msg_ = error_msg;
    return status;
  }

  SrcDst* src_dst;

 private:
  const char** out_error_msg_;
};

class V1ToOld final : public TransformerBase {
 public:
  V1ToOld(SrcDst* src_dst, const char** out_error_msg) : TransformerBase(src_dst, out_error_msg) {}

 private:
  zx_status_t TransformStructPointer(const fidl::FidlCodedStruct& src_coded_struct,
                                     const fidl::FidlCodedStruct& dst_coded_struct,
                                     const Position& position) {
    auto presence = *src_dst->Read<uint64_t>(position);
    if (!src_dst->Copy(position, sizeof(uint64_t))) {
      return ZX_ERR_BUFFER_TOO_SMALL;
    }

    if (presence != FIDL_ALLOC_PRESENT) {
      return ZX_OK;
    }

    uint32_t aligned_src_size = FIDL_ALIGN(src_coded_struct.size);
    uint32_t aligned_dst_size = FIDL_ALIGN(dst_coded_struct.size);
    const auto struct_position = Position{
        .src_inline_offset = position.src_out_of_line_offset,
        .src_out_of_line_offset = position.src_out_of_line_offset + aligned_src_size,
        .dst_inline_offset = position.dst_out_of_line_offset,
        .dst_out_of_line_offset = position.dst_out_of_line_offset + aligned_dst_size,
    };
    return TransformStruct(src_coded_struct, dst_coded_struct, struct_position, aligned_dst_size);
  }

  zx_status_t TransformStruct(const fidl::FidlCodedStruct& src_coded_struct,
                              const fidl::FidlCodedStruct& dst_coded_struct,
                              const Position& position, uint32_t dst_size) {
    // Note: we cannot use dst_coded_struct.size, and must instead rely on
    // the provided dst_size since this struct could be placed in an alignment
    // context that is larger than its inherent size.

    // Copy structs without any coded fields, and done.
    if (src_coded_struct.field_count == 0) {
      if (!src_dst->Copy(position, dst_size)) {
        return ZX_ERR_BUFFER_TOO_SMALL;
      }
      return ZX_OK;
    }

    const uint32_t src_start_of_struct = position.src_inline_offset;
    const uint32_t dst_start_of_struct = position.dst_inline_offset;
    const uint32_t dst_end_of_struct = position.dst_inline_offset + dst_size;

    auto current_position = position;
    for (uint32_t src_field_index = 0; src_field_index < src_coded_struct.field_count;
         src_field_index++) {
      const auto& src_field = src_coded_struct.fields[src_field_index];

      // Copy fields without coding tables.
      if (!src_field.type) {
        uint32_t dst_field_size = src_field.offset /*.padding_offset*/ +
                                  (src_start_of_struct - current_position.src_inline_offset);
        if (!src_dst->Copy(current_position, dst_field_size)) {
          return ZX_ERR_BUFFER_TOO_SMALL;
        }
        current_position = current_position.IncreaseInlineOffset(dst_field_size);
        continue;
      }

      assert(src_field.alt_field);
      const auto& dst_field = *src_field.alt_field;

      // Pad between fields (if needed).
      if (current_position.dst_inline_offset < dst_field.offset) {
        uint32_t padding_size = dst_field.offset - current_position.dst_inline_offset;
        src_dst->Pad(current_position, padding_size);
        current_position = current_position.IncreaseInlineOffset(padding_size);
      }

      // Set current position before transforming field.
      // Note: an alternative implementation could be to update the current
      // position so as to avoid using the provided offset. However, one of the
      // benefit and simplification of coding tables is offset based positioning
      // and this therefore feels appropriate.
      current_position.src_inline_offset = src_start_of_struct + src_field.offset;
      current_position.dst_inline_offset = dst_start_of_struct + dst_field.offset;

      // Transform field.
      uint32_t src_next_field_offset =
          current_position.src_inline_offset + AlignedInlineSize(src_field.type, WireFormat::kV1);
      uint32_t dst_next_field_offset =
          current_position.dst_inline_offset + AlignedInlineSize(dst_field.type, WireFormat::kOld);
      uint32_t dst_field_size = dst_next_field_offset - dst_field.offset;
      zx_status_t status = Transform(src_field.type, current_position, dst_field_size);
      if (status != ZX_OK) {
        return status;
      }

      // Update current position for next iteration.
      current_position.src_inline_offset = src_next_field_offset;
      current_position.dst_inline_offset = dst_next_field_offset;
    }

    // Pad end (if needed).
    if (current_position.dst_inline_offset < dst_end_of_struct) {
      uint32_t size = dst_end_of_struct - current_position.dst_inline_offset;
      src_dst->Pad(current_position, size);
    }

    return ZX_OK;
  }

  zx_status_t TransformUnion(const fidl::FidlCodedUnion& src_coded_union,
                             const fidl::FidlCodedUnion& dst_coded_union,
                             const Position& position) {
    assert(src_coded_union.field_count == dst_coded_union.field_count);

    // Read: extensible-union ordinal.
    auto src_xunion = src_dst->Read<const fidl_xunion_t>(position);
    uint32_t xunion_ordinal = src_xunion->tag;

    // Retrieve: flexible-union field (or variant).
    bool src_field_found = false;
    uint32_t src_field_index = 0;
    const fidl::FidlUnionField* src_field = nullptr;
    for (/* src_field_index needed after the loop */;
         src_field_index < src_coded_union.field_count; src_field_index++) {
      const fidl::FidlUnionField* candidate_src_field = &src_coded_union.fields[src_field_index];
      if (candidate_src_field->xunion_ordinal == xunion_ordinal) {
        src_field_found = true;
        src_field = candidate_src_field;
        break;
      }
    }
    if (!src_field_found) {
      return Fail(ZX_ERR_BAD_STATE, "ordinal has no corresponding variant");
    }

    const fidl::FidlUnionField& dst_field = dst_coded_union.fields[src_field_index];

    // Write: static-union tag, and pad (if needed).
    switch (dst_coded_union.data_offset) {
      case 4:
        src_dst->Write(position, src_field_index);
        break;
      case 8:
        src_dst->Write(position, static_cast<uint64_t>(src_field_index));
        break;
      default:
        assert(false && "static-union data offset can only be 4 or 8");
    }

    // Transform: static-union field (or variant).
    auto field_position = Position{
        .src_inline_offset = position.src_out_of_line_offset,
        .src_out_of_line_offset = position.src_out_of_line_offset
                                + AlignedInlineSize(src_field->type, WireFormat::kOld),
        .dst_inline_offset = position.dst_inline_offset + dst_coded_union.data_offset,
        .dst_out_of_line_offset = position.dst_out_of_line_offset,
    };
    uint32_t dst_field_size = dst_coded_union.size - dst_coded_union.data_offset;
    zx_status_t status = Transform(src_field->type, field_position, dst_field_size);
    if (status != ZX_OK) {
      return status;
    }

    // Pad after static-union data.
    auto field_padding_position =
        field_position.IncreaseDstInlineOffset(dst_field_size - dst_field.padding);
    src_dst->Pad(field_padding_position, dst_field.padding);

    return ZX_OK;
  }

  zx_status_t TransformString(const Position& position) {
    static const auto string_as_coded_vector = fidl::FidlCodedVector(
        nullptr /* element */, 0 /*max count, unused */, 1 /* element_size */,
        fidl::FidlNullability::kNullable /* constraints are not checked, i.e. unused */,
        nullptr /* alt_type unused, we provide both src and dst */);
    return TransformVector(string_as_coded_vector, string_as_coded_vector, position);
  }

  zx_status_t TransformVector(const fidl::FidlCodedVector& src_coded_vector,
                              const fidl::FidlCodedVector& dst_coded_vector,
                              const Position& position) {
    const auto& src_vector = *src_dst->Read<fidl_vector_t>(position);

    // Copy vector header.
    if (!src_dst->Copy(position, sizeof(fidl_vector_t))) {
      return ZX_ERR_BUFFER_TOO_SMALL;
    }

    // Early exit on nullable vectors.
    auto presence = reinterpret_cast<uint64_t>(src_vector.data);
    if (presence != FIDL_ALLOC_PRESENT) {
      return ZX_OK;
    }

    // Viewing vectors data as arrays.
    uint32_t src_element_padding =
        FIDL_ELEM_ALIGN(src_coded_vector.element_size) - src_coded_vector.element_size;
    uint32_t dst_element_padding =
        FIDL_ELEM_ALIGN(dst_coded_vector.element_size) - dst_coded_vector.element_size;
    const auto convert = [&](const fidl::FidlCodedVector& coded_vector, uint32_t element_padding) {
      return fidl::FidlCodedArrayNew(coded_vector.element,
                                     src_vector.count,
                                     coded_vector.element_size,
                                     element_padding,
                                     nullptr /* alt_type unused, we provide both src and dst */);
    };
    const auto src_vector_data_as_coded_array = convert(src_coded_vector, src_element_padding);
    const auto dst_vector_data_as_coded_array = convert(dst_coded_vector, dst_element_padding);

    // Calculate vector size. They fit in uint32_t due to automatic bounds.
    uint32_t src_vector_size =
        FIDL_ALIGN(static_cast<uint32_t>(src_vector.count * (src_coded_vector.element_size + src_element_padding)));
    uint32_t dst_vector_size =
        FIDL_ALIGN(static_cast<uint32_t>(src_vector.count * (dst_coded_vector.element_size + dst_element_padding)));

    // Transform elements.
    auto vector_data_position = Position{
      .src_inline_offset = position.src_out_of_line_offset,
      .src_out_of_line_offset = position.src_out_of_line_offset + src_vector_size,
      .dst_inline_offset = position.dst_out_of_line_offset,
      .dst_out_of_line_offset = position.dst_out_of_line_offset + dst_vector_size
    };
    zx_status_t status = TransformArray(src_vector_data_as_coded_array,
                                        dst_vector_data_as_coded_array,
                                        vector_data_position, dst_vector_size);
    if (status != ZX_OK) {
      return status;
    }

    return ZX_OK;
  }

  zx_status_t TransformArray(const fidl::FidlCodedArrayNew& src_coded_array,
                             const fidl::FidlCodedArrayNew& dst_coded_array,
                             const Position& position, uint32_t dst_array_size) {
    assert(src_coded_array.element_count == dst_coded_array.element_count);

    // Fast path for elements without coding tables (e.g. strings).
    if (!src_coded_array.element) {
      if (!src_dst->Copy(position, dst_array_size)) {
        return ZX_ERR_BUFFER_TOO_SMALL;
      }
      return ZX_OK;
    }

    // Slow path otherwise.
    auto current_element_position = position;
    for (uint32_t i = 0; i < src_coded_array.element_count; i++) {
      zx_status_t status = Transform(src_coded_array.element,
                                     current_element_position,
                                     dst_coded_array.element_size);
      if (status != ZX_OK) {
        return status;
      }

      // Pad end of an element.
      auto padding_position = current_element_position
        .IncreaseSrcInlineOffset(src_coded_array.element_size)
        .IncreaseDstInlineOffset(dst_coded_array.element_size);
      src_dst->Pad(padding_position, dst_coded_array.element_padding);

      current_element_position = padding_position
        .IncreaseSrcInlineOffset(src_coded_array.element_padding)
        .IncreaseDstInlineOffset(dst_coded_array.element_padding);
    }

    // Pad end of elements.
    uint32_t padding = dst_array_size
                     + position.dst_inline_offset
                     - current_element_position.dst_inline_offset;
    src_dst->Pad(current_element_position, padding);

    return ZX_OK;
  }
};

}  // namespace

zx_status_t fidl_transform(fidl_transformation_t transformation,
                           const fidl_type_t* type,
                           const uint8_t* src_bytes, uint32_t src_num_bytes,
                           uint8_t* dst_bytes, uint32_t* out_dst_num_bytes,
                           const char** out_error_msg) {
  assert(type);
  assert(src_bytes);
  assert(dst_bytes);
  assert(out_dst_num_bytes);

  SrcDst src_dst(src_bytes, src_num_bytes, dst_bytes, out_dst_num_bytes);
  switch (transformation) {
    case FIDL_TRANSFORMATION_NONE:
       return ZX_OK;
    case FIDL_TRANSFORMATION_V1_TO_OLD:
      return V1ToOld(&src_dst, out_error_msg).TransformTopLevelStruct(type);
    default: {
      if (out_error_msg)
        *out_error_msg = "unsupported transformation";
      return ZX_ERR_INVALID_ARGS;
    }
  }
}
