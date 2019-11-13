// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/internal.h>
#include <lib/fidl/transformer.h>

#include <cassert>
#include <cstring>

// Disable warning about implicit fallthrough, since it's intentionally used a lot in this code, and
// the switch()es end up being harder to read without it. Note that "#pragma GCC" works for both GCC
// & Clang.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"

namespace {

enum struct WireFormat {
  kOld,
  kV1,
};

// Every Transform() method outputs a TraversalResult, which indicates how many out-of-line bytes
// that transform method consumed, and the actual (not max) number of handles that were encountered
// during the transformation. This is needed for writing the correct size and handle information in
// an envelope. (This isn't a great name. A better name would be welcome!)
struct TraversalResult {
  uint32_t src_out_of_line_size = 0u;
  uint32_t dst_out_of_line_size = 0u;
  uint32_t handle_count = 0u;

  TraversalResult& operator+=(const TraversalResult rhs) {
    src_out_of_line_size += rhs.src_out_of_line_size;
    dst_out_of_line_size += rhs.dst_out_of_line_size;
    handle_count += rhs.handle_count;

    return *this;
  }
};

constexpr uint32_t PrimitiveSize(const fidl::FidlCodedPrimitive primitive) {
  switch (primitive) {
    case fidl::FidlCodedPrimitive::kBool:
    case fidl::FidlCodedPrimitive::kInt8:
    case fidl::FidlCodedPrimitive::kUint8:
      return 1;
    case fidl::FidlCodedPrimitive::kInt16:
    case fidl::FidlCodedPrimitive::kUint16:
      return 2;
    case fidl::FidlCodedPrimitive::kInt32:
    case fidl::FidlCodedPrimitive::kUint32:
    case fidl::FidlCodedPrimitive::kFloat32:
      return 4;
    case fidl::FidlCodedPrimitive::kInt64:
    case fidl::FidlCodedPrimitive::kUint64:
    case fidl::FidlCodedPrimitive::kFloat64:
      return 8;
  }
  __builtin_unreachable();
}

// This function is named UnsafeInlineSize since it assumes that |type| will be
// non-null. Don't call this function directly; instead, call
// TransformerBase::InlineSize().
uint32_t UnsafeInlineSize(const fidl_type_t* type, WireFormat wire_format) {
  assert(type);

  switch (type->type_tag) {
    case fidl::kFidlTypePrimitive:
      return PrimitiveSize(type->coded_primitive);
    case fidl::kFidlTypeEnum:
      return PrimitiveSize(type->coded_enum.underlying_type);
    case fidl::kFidlTypeBits:
      return PrimitiveSize(type->coded_bits.underlying_type);
    case fidl::kFidlTypeStructPointer:
      return 8;
    case fidl::kFidlTypeUnionPointer:
      switch (wire_format) {
        case WireFormat::kOld:
          return 8;
        case WireFormat::kV1:
          return 24;  // xunion
      }
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
      }
    case fidl::kFidlTypeArray:
      return type->coded_array.array_size;
    case fidl::kFidlTypeXUnion:
      return 24;
    case fidl::kFidlTypeHandle:
      return 4;
    case fidl::kFidlTypeTable:
      return 16;
  }

  // This is needed to suppress a GCC warning "control reaches end of non-void function", since GCC
  // treats switch() on enums as non-exhaustive without a default case.
  assert(false && "unexpected non-exhaustive switch on fidl::FidlTypeTag");
  return 0;
}

struct Position {
  uint32_t src_inline_offset = 0;
  uint32_t src_out_of_line_offset = 0;
  uint32_t dst_inline_offset = 0;
  uint32_t dst_out_of_line_offset = 0;

  Position(uint32_t src_inline_offset, uint32_t src_out_of_line_offset, uint32_t dst_inline_offset,
           uint32_t dst_out_of_line_offset)
      : src_inline_offset(src_inline_offset),
        src_out_of_line_offset(src_out_of_line_offset),
        dst_inline_offset(dst_inline_offset),
        dst_out_of_line_offset(dst_out_of_line_offset) {}

  inline Position IncreaseInlineOffset(uint32_t increase) const
      __attribute__((warn_unused_result)) {
    return IncreaseSrcInlineOffset(increase).IncreaseDstInlineOffset(increase);
  }

  inline Position IncreaseSrcInlineOffset(uint32_t increase) const
      __attribute__((warn_unused_result)) {
    return Position(src_inline_offset + increase, src_out_of_line_offset, dst_inline_offset,
                    dst_out_of_line_offset);
  }

  inline Position IncreaseSrcOutOfLineOffset(uint32_t increase) const
      __attribute__((warn_unused_result)) {
    return Position(src_inline_offset, src_out_of_line_offset + increase, dst_inline_offset,
                    dst_out_of_line_offset);
  }

  inline Position IncreaseDstInlineOffset(uint32_t increase) const
      __attribute__((warn_unused_result)) {
    return Position(src_inline_offset, src_out_of_line_offset, dst_inline_offset + increase,
                    dst_out_of_line_offset);
  }

  inline Position IncreaseDstOutOfLineOffset(uint32_t increase) const
      __attribute__((warn_unused_result)) {
    return Position(src_inline_offset, src_out_of_line_offset, dst_inline_offset,
                    dst_out_of_line_offset + increase);
  }
};

class SrcDst final {
 public:
  SrcDst(const uint8_t* src_bytes, const uint32_t src_num_bytes, uint8_t* dst_bytes,
         uint32_t dst_num_bytes_capacity, uint32_t* out_dst_num_bytes)
      : src_bytes_(src_bytes),
        src_num_bytes_(src_num_bytes),
        dst_bytes_(dst_bytes),
        dst_num_bytes_capacity_(dst_num_bytes_capacity),
        out_dst_num_bytes_(out_dst_num_bytes) {}
  SrcDst(const SrcDst&) = delete;

  ~SrcDst() { *out_dst_num_bytes_ = dst_max_offset_; }

  template <typename T>
  const T* __attribute__((warn_unused_result)) Read(const Position& position) const {
    uint32_t size = sizeof(T);
    if (!(position.src_inline_offset + size <= src_num_bytes_)) {
      return nullptr;
    }

    return reinterpret_cast<const T*>(src_bytes_ + position.src_inline_offset);
  }

  zx_status_t __attribute__((warn_unused_result)) Copy(const Position& position, uint32_t size) {
    if (!(position.src_inline_offset + size <= src_num_bytes_)) {
      return ZX_ERR_BAD_STATE;
    }

    const auto status = UpdateMaxOffset(position.dst_inline_offset + size);
    if (status != ZX_OK) {
      return status;
    }
    memcpy(dst_bytes_ + position.dst_inline_offset, src_bytes_ + position.src_inline_offset, size);
    return ZX_OK;
  }

  zx_status_t __attribute__((warn_unused_result)) Pad(const Position& position, uint32_t size) {
    const auto status = UpdateMaxOffset(position.dst_inline_offset + size);
    if (status != ZX_OK) {
      return status;
    }
    memset(dst_bytes_ + position.dst_inline_offset, 0, size);
    return ZX_OK;
  }

  template <typename T>
  zx_status_t __attribute__((warn_unused_result)) Write(const Position& position, T value) {
    const auto size = static_cast<uint32_t>(sizeof(value));
    const auto status = UpdateMaxOffset(position.dst_inline_offset + size);
    if (status != ZX_OK) {
      return status;
    }
    auto ptr = reinterpret_cast<T*>(dst_bytes_ + position.dst_inline_offset);
    *ptr = value;
    return ZX_OK;
  }

 private:
  zx_status_t __attribute__((warn_unused_result)) UpdateMaxOffset(uint32_t dst_offset) {
    if (dst_offset > dst_max_offset_) {
      if (!(dst_offset <= dst_num_bytes_capacity_)) {
        return ZX_ERR_BAD_STATE;
      }
      dst_max_offset_ = dst_offset;
    }
    return ZX_OK;
  }

  const uint8_t* src_bytes_;
  const uint32_t src_num_bytes_;
  uint8_t* dst_bytes_;
  const uint32_t dst_num_bytes_capacity_;
  uint32_t* out_dst_num_bytes_;

  uint32_t dst_max_offset_ = 0;
};

class TransformerBase {
 public:
  TransformerBase(SrcDst* src_dst, const char** out_error_msg)
      : src_dst(src_dst), out_error_msg_(out_error_msg) {}
  virtual ~TransformerBase() = default;

  uint32_t InlineSize(const fidl_type_t* type, WireFormat wire_format) {
    assert(type);

    return UnsafeInlineSize(type, wire_format);
  }

  uint32_t AltInlineSize(const fidl_type_t* type) {
    assert(type);

    switch (type->type_tag) {
      case fidl::kFidlTypeStruct: {
        const fidl_type_t ft(*type->coded_struct.alt_type);
        return InlineSize(&ft, To());
      }
      case fidl::kFidlTypeUnion: {
        const fidl_type_t ft(*type->coded_union.alt_type);
        return InlineSize(&ft, To());
      }
      case fidl::kFidlTypeArray: {
        const fidl_type_t ft(*type->coded_array.alt_type);
        return InlineSize(&ft, To());
      }
      case fidl::kFidlTypePrimitive:
      case fidl::kFidlTypeEnum:
      case fidl::kFidlTypeBits:
      case fidl::kFidlTypeStructPointer:
      case fidl::kFidlTypeUnionPointer:
      case fidl::kFidlTypeVector:
      case fidl::kFidlTypeString:
      case fidl::kFidlTypeXUnion:
      case fidl::kFidlTypeHandle:
      case fidl::kFidlTypeTable:
        return InlineSize(type, To());
    }

    assert(false && "unexpected non-exhaustive switch on fidl::FidlTypeTag");
    return 0;
  }

  zx_status_t TransformTopLevelStruct(const fidl_type_t* type) {
    if (type->type_tag != fidl::kFidlTypeStruct) {
      return Fail(ZX_ERR_INVALID_ARGS, "only top-level structs supported");
    }

    const auto& src_coded_struct = type->coded_struct;
    const auto& dst_coded_struct = *src_coded_struct.alt_type;
    // Since this is the top-level struct, the first secondary object (i.e.
    // out-of-line offset) is exactly placed after this struct, i.e. the
    // struct's inline size.
    const auto start_position = Position(0, src_coded_struct.size, 0, dst_coded_struct.size);

    TraversalResult discarded_traversal_result;
    return TransformStruct(src_coded_struct, dst_coded_struct, start_position,
                           FIDL_ALIGN(dst_coded_struct.size), &discarded_traversal_result);
  }

 protected:
  zx_status_t Transform(const fidl_type_t* type, const Position& position, const uint32_t dst_size,
                        TraversalResult* out_traversal_result) {
    if (!type) {
      return src_dst->Copy(position, dst_size);
    }
    switch (type->type_tag) {
      case fidl::kFidlTypeHandle:
        return TransformHandle(position, dst_size, out_traversal_result);
      case fidl::kFidlTypePrimitive:
      case fidl::kFidlTypeEnum:
      case fidl::kFidlTypeBits:
        return src_dst->Copy(position, dst_size);
      case fidl::kFidlTypeStructPointer: {
        const auto& src_coded_struct = *type->coded_struct_pointer.struct_type;
        const auto& dst_coded_struct = *src_coded_struct.alt_type;
        return TransformStructPointer(src_coded_struct, dst_coded_struct, position,
                                      out_traversal_result);
      }
      case fidl::kFidlTypeUnionPointer: {
        const auto& src_coded_union = *type->coded_union_pointer.union_type;
        const auto& dst_coded_union = *src_coded_union.alt_type;
        return TransformUnionPointer(src_coded_union, dst_coded_union, position,
                                     out_traversal_result);
      }
      case fidl::kFidlTypeStruct: {
        const auto& src_coded_struct = type->coded_struct;
        const auto& dst_coded_struct = *src_coded_struct.alt_type;
        return TransformStruct(src_coded_struct, dst_coded_struct, position, dst_size,
                               out_traversal_result);
      }
      case fidl::kFidlTypeUnion: {
        const auto& src_coded_union = type->coded_union;
        const auto& dst_coded_union = *src_coded_union.alt_type;
        return TransformUnion(src_coded_union, dst_coded_union, position, dst_size,
                              out_traversal_result);
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
        return TransformArray(src_coded_array, dst_coded_array, position, dst_size,
                              out_traversal_result);
      }
      case fidl::kFidlTypeString:
        return TransformString(position, out_traversal_result);
      case fidl::kFidlTypeVector: {
        const auto& src_coded_vector = type->coded_vector;
        const auto& dst_coded_vector = *src_coded_vector.alt_type;
        return TransformVector(src_coded_vector, dst_coded_vector, position, out_traversal_result);
      }
      case fidl::kFidlTypeTable:
        return TransformTable(type->coded_table, position, out_traversal_result);
      case fidl::kFidlTypeXUnion:
        return TransformXUnion(type->coded_xunion, position, out_traversal_result);
    }

    return Fail(ZX_ERR_BAD_STATE, "unknown type tag");

    // TODO(apang): Think about putting logic for updating out_traversal_result in Copy/etc
    // functions.
  }

  zx_status_t TransformHandle(const Position& position, uint32_t dst_size,
                              TraversalResult* out_traversal_result) {
    auto presence = src_dst->Read<uint32_t>(position);
    if (!presence) {
      return Fail(ZX_ERR_BAD_STATE, "handle presence missing");
    }
    switch (*presence) {
      case FIDL_HANDLE_ABSENT:
        // Ok
        break;
      case FIDL_HANDLE_PRESENT:
        out_traversal_result->handle_count++;
        break;
      default:
        return Fail(ZX_ERR_BAD_STATE, "handle presence invalid");
    }
    return src_dst->Copy(position, dst_size);
  }

  zx_status_t TransformStructPointer(const fidl::FidlCodedStruct& src_coded_struct,
                                     const fidl::FidlCodedStruct& dst_coded_struct,
                                     const Position& position,
                                     TraversalResult* out_traversal_result) {
    auto presence = src_dst->Read<uint64_t>(position);
    if (!presence) {
      return Fail(ZX_ERR_BAD_STATE, "struct pointer missing");
    }

    auto status_copy_struct_pointer = src_dst->Copy(position, sizeof(uint64_t));
    if (status_copy_struct_pointer != ZX_OK) {
      return status_copy_struct_pointer;
    }

    switch (*presence) {
      case FIDL_ALLOC_ABSENT:
        // Early exit on absent struct.
        return ZX_OK;
      case FIDL_ALLOC_PRESENT:
        // Ok
        break;
      default:
        return Fail(ZX_ERR_BAD_STATE, "struct pointer invalid");
    }

    uint32_t src_aligned_size = FIDL_ALIGN(src_coded_struct.size);
    uint32_t dst_aligned_size = FIDL_ALIGN(dst_coded_struct.size);
    const auto struct_position = Position{
        position.src_out_of_line_offset,
        position.src_out_of_line_offset + src_aligned_size,
        position.dst_out_of_line_offset,
        position.dst_out_of_line_offset + dst_aligned_size,
    };

    out_traversal_result->src_out_of_line_size += src_aligned_size;
    out_traversal_result->dst_out_of_line_size += dst_aligned_size;

    return TransformStruct(src_coded_struct, dst_coded_struct, struct_position, dst_aligned_size,
                           out_traversal_result);
  }

  zx_status_t TransformStruct(const fidl::FidlCodedStruct& src_coded_struct,
                              const fidl::FidlCodedStruct& dst_coded_struct,
                              const Position& position, uint32_t dst_size,
                              TraversalResult* out_traversal_result) {
    assert(src_coded_struct.field_count == dst_coded_struct.field_count);
    // Note: we cannot use dst_coded_struct.size, and must instead rely on
    // the provided dst_size since this struct could be placed in an alignment
    // context that is larger than its inherent size.

    // Copy structs without any coded fields, and done.
    if (src_coded_struct.field_count == 0) {
      return src_dst->Copy(position, dst_size);
    }

    const uint32_t src_start_of_struct = position.src_inline_offset;
    const uint32_t dst_start_of_struct = position.dst_inline_offset;

    auto current_position = position;
    for (uint32_t field_index = 0; field_index < src_coded_struct.field_count; field_index++) {
      const auto& src_field = src_coded_struct.fields[field_index];
      const auto& dst_field = dst_coded_struct.fields[field_index];

      if (!src_field.type) {
        const uint32_t dst_field_size =
            src_start_of_struct + src_field.padding_offset - current_position.src_inline_offset;
        const auto status_copy_field = src_dst->Copy(current_position, dst_field_size);
        if (status_copy_field != ZX_OK) {
          return status_copy_field;
        }
        current_position = current_position.IncreaseInlineOffset(dst_field_size);
      } else {
        // The only case where the amount we've written shouldn't match the specified offset is
        // for request/response structs, where the txn header is not specified in the coding table.
        if (current_position.src_inline_offset != src_start_of_struct + src_field.offset) {
          assert(src_field.offset == dst_field.offset);
          const auto status_copy_field = src_dst->Copy(current_position, src_field.offset);
          if (status_copy_field != ZX_OK) {
            return status_copy_field;
          }
          current_position = current_position.IncreaseInlineOffset(src_field.offset);
        }
        assert(current_position.src_inline_offset == src_start_of_struct + src_field.offset);
        assert(current_position.dst_inline_offset == dst_start_of_struct + dst_field.offset);

        // Transform field.
        uint32_t src_next_field_offset =
            current_position.src_inline_offset + InlineSize(src_field.type, From());
        uint32_t dst_next_field_offset =
            current_position.dst_inline_offset + InlineSize(dst_field.type, To());
        uint32_t dst_field_size = dst_next_field_offset - (dst_start_of_struct + dst_field.offset);

        TraversalResult field_traversal_result;
        const zx_status_t status =
            Transform(src_field.type, current_position, dst_field_size, &field_traversal_result);
        if (status != ZX_OK) {
          return status;
        }

        *out_traversal_result += field_traversal_result;

        // Update current position for next iteration.
        current_position.src_inline_offset = src_next_field_offset;
        current_position.dst_inline_offset = dst_next_field_offset;
        current_position.src_out_of_line_offset += field_traversal_result.src_out_of_line_size;
        current_position.dst_out_of_line_offset += field_traversal_result.dst_out_of_line_size;
      }

      // Pad (possibly with 0 bytes).
      const auto status_pad = src_dst->Pad(current_position, dst_field.padding);
      if (status_pad != ZX_OK) {
        return Fail(status_pad, "unable to pad end of struct element");
      }
      current_position = current_position.IncreaseDstInlineOffset(dst_field.padding);
      current_position = current_position.IncreaseSrcInlineOffset(src_field.padding);
    }

    // Pad (possibly with 0 bytes).
    const uint32_t dst_end_of_struct = position.dst_inline_offset + dst_size;
    const auto status_pad =
        src_dst->Pad(current_position, dst_end_of_struct - current_position.dst_inline_offset);
    if (status_pad != ZX_OK) {
      return Fail(status_pad, "unable to pad end of struct");
    }

    return ZX_OK;
  }

  zx_status_t TransformVector(const fidl::FidlCodedVector& src_coded_vector,
                              const fidl::FidlCodedVector& dst_coded_vector,
                              const Position& position, TraversalResult* out_traversal_result) {
    const auto src_vector = src_dst->Read<fidl_vector_t>(position);
    if (!src_vector) {
      return Fail(ZX_ERR_BAD_STATE, "vector missing");
    }

    // Copy vector header.
    const auto status_copy_vector_hdr = src_dst->Copy(position, sizeof(fidl_vector_t));
    if (status_copy_vector_hdr != ZX_OK) {
      return status_copy_vector_hdr;
    }

    const auto presence = reinterpret_cast<uint64_t>(src_vector->data);
    switch (presence) {
      case FIDL_ALLOC_ABSENT:
        // Early exit on nullable vectors.
        return ZX_OK;
      case FIDL_ALLOC_PRESENT:
        // OK
        break;
      default:
        return Fail(ZX_ERR_BAD_STATE, "vector presence invalid");
    }

    const auto convert = [&](const fidl::FidlCodedVector& coded_vector) {
      return fidl::FidlCodedArrayNew(coded_vector.element, static_cast<uint32_t>(src_vector->count),
                                     coded_vector.element_size, 0,
                                     nullptr /* alt_type unused, we provide both src and dst */);
    };
    const auto src_vector_data_as_coded_array = convert(src_coded_vector);
    const auto dst_vector_data_as_coded_array = convert(dst_coded_vector);

    // Calculate vector size. They fit in uint32_t due to automatic bounds.
    uint32_t src_vector_size =
        FIDL_ALIGN(static_cast<uint32_t>(src_vector->count * (src_coded_vector.element_size)));
    uint32_t dst_vector_size =
        FIDL_ALIGN(static_cast<uint32_t>(src_vector->count * (dst_coded_vector.element_size)));

    // Transform elements.
    auto vector_data_position = Position{
        position.src_out_of_line_offset, position.src_out_of_line_offset + src_vector_size,
        position.dst_out_of_line_offset, position.dst_out_of_line_offset + dst_vector_size};

    const zx_status_t status =
        TransformArray(src_vector_data_as_coded_array, dst_vector_data_as_coded_array,
                       vector_data_position, dst_vector_size, out_traversal_result);
    if (status != ZX_OK) {
      return status;
    }

    out_traversal_result->src_out_of_line_size += src_vector_size;
    out_traversal_result->dst_out_of_line_size += dst_vector_size;

    return ZX_OK;
  }

  zx_status_t TransformString(const Position& position, TraversalResult* out_traversal_result) {
    static const auto string_as_coded_vector = fidl::FidlCodedVector(
        nullptr /* element */, 0 /*max count, unused */, 1 /* element_size */,
        fidl::FidlNullability::kNullable /* constraints are not checked, i.e. unused */,
        nullptr /* alt_type unused, we provide both src and dst */);
    return TransformVector(string_as_coded_vector, string_as_coded_vector, position,
                           out_traversal_result);
  }

  // TODO(apang): Replace |known_type| & |type| parameters below with a single fit::optional<const
  // fidl_type_t*> parameter.
  zx_status_t TransformEnvelope(bool known_type, const fidl_type_t* type, const Position& position,
                                TraversalResult* out_traversal_result) {
    auto src_envelope = src_dst->Read<const fidl_envelope_t>(position);
    if (!src_envelope) {
      return Fail(ZX_ERR_BAD_STATE, "envelope missing");
    }

    switch (src_envelope->presence) {
      case FIDL_ALLOC_ABSENT: {
        const auto status = src_dst->Copy(position, sizeof(fidl_envelope_t));
        if (status != ZX_OK) {
          return Fail(status, "unable to copy envelope header");
        }
        return ZX_OK;
      }
      case FIDL_ALLOC_PRESENT:
        // We write the transformed envelope after the transformation, since
        // the num_bytes may be different in the dst.
        break;
      default:
        return Fail(ZX_ERR_BAD_STATE, "envelope presence invalid");
    }

    if (!known_type) {
      // Unknown type, so we don't know what type of data the envelope contains.
      const auto data_position =
          Position{position.src_out_of_line_offset,
                   position.src_out_of_line_offset + src_envelope->num_bytes,
                   position.dst_out_of_line_offset,
                   position.dst_out_of_line_offset + src_envelope->num_bytes};
      const auto status = src_dst->Copy(data_position, src_envelope->num_bytes);
      if (status != ZX_OK) {
        return Fail(status, "unable to copy envelope (unknown type)");
      }
      out_traversal_result->src_out_of_line_size += src_envelope->num_bytes;
      out_traversal_result->dst_out_of_line_size += src_envelope->num_bytes;
      return ZX_OK;
    }

    const uint32_t src_contents_inline_size = [&] {
      if (!type) {
        // The envelope contents are either a primitive or an array of primitives,
        // because |type| is nullptr. There's no size information
        // available for the type in the coding tables, but since the data is a
        // primitive or array of primitives, there can never be any out-of-line
        // data, so it's safe to use the envelope's num_bytes to determine the
        // content's inline size.
        return src_envelope->num_bytes;
      }

      return InlineSize(type, From());
    }();

    const uint32_t dst_contents_inline_size = FIDL_ALIGN(AltInlineSize(type));
    Position data_position = Position{position.src_out_of_line_offset,
                                      position.src_out_of_line_offset + src_contents_inline_size,
                                      position.dst_out_of_line_offset,
                                      position.dst_out_of_line_offset + dst_contents_inline_size};
    TraversalResult contents_traversal_result;
    zx_status_t result =
        Transform(type, data_position, dst_contents_inline_size, &contents_traversal_result);
    if (result != ZX_OK) {
      return result;
    }

    const uint32_t src_contents_size =
        FIDL_ALIGN(src_contents_inline_size) + contents_traversal_result.src_out_of_line_size;
    const uint32_t dst_contents_size =
        dst_contents_inline_size + contents_traversal_result.dst_out_of_line_size;

    fidl_envelope_t dst_envelope = *src_envelope;
    dst_envelope.num_bytes = dst_contents_size;
    const auto status_write = src_dst->Write(position, dst_envelope);
    if (status_write != ZX_OK) {
      return Fail(status_write, "unable to write envelope");
    }

    out_traversal_result->src_out_of_line_size += src_contents_size;
    out_traversal_result->dst_out_of_line_size += dst_contents_size;
    out_traversal_result->handle_count += src_envelope->num_handles;

    return ZX_OK;
  }

  zx_status_t TransformXUnion(const fidl::FidlCodedXUnion& coded_xunion, const Position& position,
                              TraversalResult* out_traversal_result) {
    auto xunion = src_dst->Read<const fidl_xunion_t>(position);
    if (!xunion) {
      return Fail(ZX_ERR_BAD_STATE, "xunion missing");
    }

    const auto status_copy_xunion_hdr = src_dst->Copy(position, sizeof(fidl_xunion_t));
    if (status_copy_xunion_hdr != ZX_OK) {
      return status_copy_xunion_hdr;
    }

    const fidl::FidlXUnionField* field = nullptr;
    for (uint32_t i = 0; i < coded_xunion.field_count; i++) {
      const fidl::FidlXUnionField* candidate_field = coded_xunion.fields + i;
      if (candidate_field->ordinal == xunion->tag) {
        field = candidate_field;
        break;
      }
    }

    const Position envelope_position = {
        position.src_inline_offset + static_cast<uint32_t>(offsetof(fidl_xunion_t, envelope)),
        position.src_out_of_line_offset,
        position.dst_inline_offset + static_cast<uint32_t>(offsetof(fidl_xunion_t, envelope)),
        position.dst_out_of_line_offset,
    };

    return TransformEnvelope(field != nullptr, field ? field->type : nullptr, envelope_position,
                             out_traversal_result);
  }

  zx_status_t TransformTable(const fidl::FidlCodedTable& coded_table, const Position& position,
                             TraversalResult* out_traversal_result) {
    auto table = src_dst->Read<const fidl_table_t>(position);
    if (!table) {
      return Fail(ZX_ERR_BAD_STATE, "table header missing");
    }

    const auto status_copy_table_hdr = src_dst->Copy(position, sizeof(fidl_table_t));
    if (status_copy_table_hdr != ZX_OK) {
      return Fail(status_copy_table_hdr, "unable to copy table header");
    }

    const uint32_t envelopes_vector_size =
        static_cast<uint32_t>(table->envelopes.count * sizeof(fidl_envelope_t));
    out_traversal_result->src_out_of_line_size += envelopes_vector_size;
    out_traversal_result->dst_out_of_line_size += envelopes_vector_size;

    auto current_envelope_position = Position{
        position.src_out_of_line_offset,
        position.src_out_of_line_offset + envelopes_vector_size,
        position.dst_out_of_line_offset,
        position.dst_out_of_line_offset + envelopes_vector_size,
    };
    for (uint32_t ordinal = 1, field_index = 0; ordinal <= table->envelopes.count; ordinal++) {
      const fidl::FidlTableField& field = coded_table.fields[field_index];

      const fidl_type_t* field_type = nullptr;
      if (ordinal == field.ordinal) {
        field_type = field.type;
        field_index++;
      }

      TraversalResult envelope_traversal_result;
      zx_status_t status = TransformEnvelope(true, field_type, current_envelope_position,
                                             &envelope_traversal_result);
      if (status != ZX_OK) {
        return status;
      }

      current_envelope_position.src_inline_offset +=
        static_cast<uint32_t>(sizeof(fidl_envelope_t));
      current_envelope_position.dst_inline_offset +=
        static_cast<uint32_t>(sizeof(fidl_envelope_t));
      current_envelope_position.src_out_of_line_offset +=
          envelope_traversal_result.src_out_of_line_size;
      current_envelope_position.dst_out_of_line_offset +=
          envelope_traversal_result.dst_out_of_line_size;

      *out_traversal_result += envelope_traversal_result;
    }

    return ZX_OK;
  }

  zx_status_t TransformArray(const fidl::FidlCodedArrayNew& src_coded_array,
                             const fidl::FidlCodedArrayNew& dst_coded_array,
                             const Position& position, uint32_t dst_array_size,
                             TraversalResult* out_traversal_result) {
    assert(src_coded_array.element_count == dst_coded_array.element_count);

    // Fast path for elements without coding tables (e.g. strings).
    if (!src_coded_array.element) {
      return src_dst->Copy(position, dst_array_size);
    }

    // Slow path otherwise.
    auto current_element_position = position;
    for (uint32_t i = 0; i < src_coded_array.element_count; i++) {
      TraversalResult element_traversal_result;
      const zx_status_t status = Transform(src_coded_array.element, current_element_position,
                                           dst_coded_array.element_size, &element_traversal_result);

      if (status != ZX_OK) {
        return status;
      }

      // Pad end of an element.
      auto padding_position =
          current_element_position.IncreaseSrcInlineOffset(src_coded_array.element_size)
              .IncreaseDstInlineOffset(dst_coded_array.element_size);
      const auto status_pad = src_dst->Pad(padding_position, dst_coded_array.element_padding);
      if (status_pad != ZX_OK) {
        return Fail(status_pad, "unable to pad array element");
      }

      current_element_position =
          padding_position.IncreaseSrcInlineOffset(src_coded_array.element_padding)
              .IncreaseDstInlineOffset(dst_coded_array.element_padding)
              .IncreaseSrcOutOfLineOffset(element_traversal_result.src_out_of_line_size)
              .IncreaseDstOutOfLineOffset(element_traversal_result.dst_out_of_line_size);

      *out_traversal_result += element_traversal_result;
    }

    // Pad end of elements.
    uint32_t padding =
        dst_array_size + position.dst_inline_offset - current_element_position.dst_inline_offset;
    const auto status_pad = src_dst->Pad(current_element_position, padding);
    if (status_pad != ZX_OK) {
      return Fail(status_pad, "unable to pad end of array");
    }

    return ZX_OK;
  }

  virtual WireFormat From() const = 0;
  virtual WireFormat To() const = 0;

  virtual zx_status_t TransformUnionPointer(const fidl::FidlCodedUnion& src_coded_union,
                                            const fidl::FidlCodedUnion& dst_coded_union,
                                            const Position& position,
                                            TraversalResult* out_traversal_result) = 0;

  virtual zx_status_t TransformUnion(const fidl::FidlCodedUnion& src_coded_union,
                                     const fidl::FidlCodedUnion& dst_coded_union,
                                     const Position& position, uint32_t dst_size,
                                     TraversalResult* out_traversal_result) = 0;

  inline zx_status_t Fail(zx_status_t status, const char* error_msg) {
    if (out_error_msg_)
      *out_error_msg_ = error_msg;
    return status;
  }

  SrcDst* src_dst;

 private:
  const char** out_error_msg_;
};

// TODO(apang): Mark everything override

class V1ToOld final : public TransformerBase {
 public:
  V1ToOld(SrcDst* src_dst, const char** out_error_msg) : TransformerBase(src_dst, out_error_msg) {}

  WireFormat From() const { return WireFormat::kV1; }
  WireFormat To() const { return WireFormat::kOld; }

  zx_status_t TransformUnionPointer(const fidl::FidlCodedUnion& src_coded_union,
                                    const fidl::FidlCodedUnion& dst_coded_union,
                                    const Position& position,
                                    TraversalResult* out_traversal_result) {
    auto src_xunion = src_dst->Read<const fidl_xunion_t>(position);
    if (!src_xunion) {
      return Fail(ZX_ERR_BAD_STATE, "union-as-xunion missing");
    }

    switch (src_xunion->envelope.presence) {
      case FIDL_ALLOC_ABSENT:
      case FIDL_ALLOC_PRESENT: {
        const auto status = src_dst->Write(position, src_xunion->envelope.presence);
        if (status != ZX_OK) {
          return Fail(status, "unable to write union pointer absence");
        }
        if (src_xunion->envelope.presence == FIDL_ALLOC_ABSENT) {
          return ZX_OK;
        }
        break;
      }
      default:
        return Fail(ZX_ERR_BAD_STATE, "union-as-xunion envelope presence invalid");
    }

    const uint32_t dst_aligned_size = FIDL_ALIGN(dst_coded_union.size);
    const auto union_position = Position{
        position.src_inline_offset,
        position.src_out_of_line_offset,
        position.dst_out_of_line_offset,
        position.dst_out_of_line_offset + dst_aligned_size,
    };

    out_traversal_result->dst_out_of_line_size += dst_aligned_size;

    return TransformUnion(src_coded_union, dst_coded_union, union_position,
                          FIDL_ALIGN(dst_coded_union.size), out_traversal_result);
  }

  zx_status_t TransformUnion(const fidl::FidlCodedUnion& src_coded_union,
                             const fidl::FidlCodedUnion& dst_coded_union,
                             const Position& position, uint32_t dst_size,
                             TraversalResult* out_traversal_result) {
    assert(src_coded_union.field_count == dst_coded_union.field_count);

    // Read: extensible-union ordinal.
    const auto src_xunion = src_dst->Read<const fidl_xunion_t>(position);
    if (!src_xunion) {
      return Fail(ZX_ERR_BAD_STATE, "union-as-xunion missing");
    }

    if (src_xunion->padding != static_cast<decltype(src_xunion->padding)>(0)) {
      return Fail(ZX_ERR_BAD_STATE, "union-as-xunion padding is non-zero");
    }

    switch (src_xunion->envelope.presence) {
      case FIDL_ALLOC_PRESENT:
        // OK
        break;
      case FIDL_ALLOC_ABSENT:
        return Fail(ZX_ERR_BAD_STATE, "union-as-xunion envelope is invalid FIDL_ALLOC_ABSENT");
      default:
        return Fail(ZX_ERR_BAD_STATE, "union-as-xunion envelope presence invalid");
    }

    // Retrieve: flexible-union field (or variant).
    bool src_field_found = false;
    uint32_t src_field_index = 0;
    const fidl::FidlUnionField* src_field = nullptr;
    for (/* src_field_index needed after the loop */; src_field_index < src_coded_union.field_count;
         src_field_index++) {
      const fidl::FidlUnionField* candidate_src_field = &src_coded_union.fields[src_field_index];
      if (candidate_src_field->xunion_ordinal == src_xunion->tag) {
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
      case 4: {
        const auto status = src_dst->Write(position, src_field_index);
        if (status != ZX_OK) {
          return Fail(status, "unable to write union tag");
        }
        break;
      }
      case 8: {
        const auto status = src_dst->Write(position, static_cast<uint64_t>(src_field_index));
        if (status != ZX_OK) {
          return Fail(status, "unable to write union tag");
        }
        break;
      }
      default:
        assert(false && "static-union data offset can only be 4 or 8");
    }

    // TODO(apang): The code below also in TransformEnvelope(). We should
    // refactor this method to call TransformEnvelope() if possible, instead of
    // re-implementing parts of it here.
    const uint32_t src_field_inline_size = [&] {
      if (!src_field->type) {
        // src_field's type is either a primitive or an array of primitives,
        // because src_field->type is nullptr. There's no size information
        // available for the field in the coding tables, but since the data is a
        // primitive or array of primitives, there can never be any out-of-line
        // data, so it's safe to use the envelope's num_bytes to determine the
        // field's inline size.
        return src_xunion->envelope.num_bytes;
      }

      return FIDL_ALIGN(InlineSize(src_field->type, From()));
    }();

    // Transform: xunion field to static-union field (or variant).
    auto field_position = Position{
        position.src_out_of_line_offset,
        position.src_out_of_line_offset + src_field_inline_size,
        position.dst_inline_offset + dst_coded_union.data_offset,
        position.dst_out_of_line_offset,
    };
    uint32_t dst_field_unpadded_size =
        dst_coded_union.size - dst_coded_union.data_offset - dst_field.padding;

    zx_status_t status =
        Transform(src_field->type, field_position, dst_field_unpadded_size, out_traversal_result);
    if (status != ZX_OK) {
      return status;
    }

    // Pad after static-union data.
    auto field_padding_position = field_position.IncreaseDstInlineOffset(dst_field_unpadded_size);
    const auto dst_padding = (dst_size - dst_coded_union.size) + dst_field.padding;
    const auto status_pad_field = src_dst->Pad(field_padding_position, dst_padding);
    if (status_pad_field != ZX_OK) {
      return Fail(status_pad_field, "unable to pad union variant");
    }

    out_traversal_result->src_out_of_line_size += src_field_inline_size;

    return ZX_OK;
  }
};

class OldToV1 final : public TransformerBase {
 public:
  OldToV1(SrcDst* src_dst, const char** out_error_msg) : TransformerBase(src_dst, out_error_msg) {}

 private:
  // TODO(apang): Could CRTP this.
  WireFormat From() const { return WireFormat::kOld; }
  WireFormat To() const { return WireFormat::kV1; }

  zx_status_t TransformUnionPointer(const fidl::FidlCodedUnion& src_coded_union,
                                    const fidl::FidlCodedUnion& dst_coded_union,
                                    const Position& position,
                                    TraversalResult* out_traversal_result) {
    auto presence = src_dst->Read<uint64_t>(position);
    if (!presence) {
      return Fail(ZX_ERR_BAD_STATE, "union pointer missing");
    }

    switch (*presence) {
      case FIDL_ALLOC_ABSENT: {
        fidl_xunion_t absent = {};
        const auto status = src_dst->Write(position, absent);
        if (status != ZX_OK) {
          return Fail(status, "unbale to write union pointer absense");
        }
        return ZX_OK;
      }
      case FIDL_ALLOC_PRESENT:
        // Ok
        break;
      default:
        return Fail(ZX_ERR_BAD_STATE, "union pointer invalid");
    }

    uint32_t src_aligned_size = FIDL_ALIGN(src_coded_union.size);
    const auto union_position = Position{
        position.src_out_of_line_offset,
        position.src_out_of_line_offset + src_aligned_size,
        position.dst_inline_offset,
        position.dst_out_of_line_offset,
    };

    out_traversal_result->src_out_of_line_size += src_aligned_size;
    return TransformUnion(src_coded_union, dst_coded_union, union_position,
                          0 /* unused: xunions are FIDL_ALIGNed */,
                          out_traversal_result);
  }

  zx_status_t TransformUnion(const fidl::FidlCodedUnion& src_coded_union,
                             const fidl::FidlCodedUnion& dst_coded_union,
                             const Position& position, uint32_t /* unused: dst_size */,
                             TraversalResult* out_traversal_result) {
    assert(src_coded_union.field_count == dst_coded_union.field_count);

    // Read: union tag.
    const auto union_tag_ptr = src_dst->Read<const fidl_union_tag_t>(position);
    if (!union_tag_ptr) {
      return Fail(ZX_ERR_BAD_STATE, "union tag missing");
    }
    const auto union_tag = *union_tag_ptr;

    // Retrieve: union field/variant.
    if (union_tag >= src_coded_union.field_count) {
      return Fail(ZX_ERR_BAD_STATE, "invalid union tag");
    }

    const fidl::FidlUnionField& src_field = src_coded_union.fields[union_tag];
    const fidl::FidlUnionField& dst_field = dst_coded_union.fields[union_tag];

    // Write: xunion tag & envelope.
    const uint32_t dst_inline_field_size = [&] {
      if (src_field.type && src_field.type->type_tag == fidl::kFidlTypeUnion) {
        return 24u;
      } else {
        return src_coded_union.size - src_coded_union.data_offset - src_field.padding;
      }
    }();

    // Transform: static-union field to xunion field.
    auto field_position = Position{
        position.src_inline_offset + src_coded_union.data_offset,
        position.src_out_of_line_offset,
        position.dst_out_of_line_offset,
        position.dst_out_of_line_offset + FIDL_ALIGN(dst_inline_field_size),
    };
    TraversalResult field_traversal_result;
    zx_status_t status =
        Transform(src_field.type, field_position, dst_inline_field_size, &field_traversal_result);
    if (status != ZX_OK) {
      return status;
    }

    // Pad field (if needed).
    const uint32_t dst_field_size =
        dst_inline_field_size + field_traversal_result.dst_out_of_line_size;
    const uint32_t dst_padding = FIDL_ALIGN(dst_field_size) - dst_field_size;
    const auto status_pad_field =
        src_dst->Pad(field_position.IncreaseDstInlineOffset(dst_field_size), dst_padding);
    if (status_pad_field != ZX_OK) {
      return Fail(status_pad_field, "unable to pad union-as-xunion variant");
    }

    // Write envelope header.
    fidl_xunion_t xunion;
    xunion.tag = dst_field.xunion_ordinal;
    xunion.padding = 0;
    xunion.envelope.num_bytes = FIDL_ALIGN(dst_field_size);
    xunion.envelope.num_handles = field_traversal_result.handle_count;
    xunion.envelope.presence = FIDL_ALLOC_PRESENT;
    const auto status_write_xunion = src_dst->Write(position, xunion);
    if (status_write_xunion != ZX_OK) {
      return Fail(status_write_xunion, "unable to write union-as-xunion header");
    }

    out_traversal_result->src_out_of_line_size += field_traversal_result.src_out_of_line_size;
    out_traversal_result->dst_out_of_line_size += FIDL_ALIGN(dst_field_size);
    out_traversal_result->handle_count += field_traversal_result.handle_count;

    return ZX_OK;
  }
};

}  // namespace

zx_status_t fidl_transform(fidl_transformation_t transformation, const fidl_type_t* type,
                           const uint8_t* src_bytes, uint32_t src_num_bytes, uint8_t* dst_bytes,
                           uint32_t dst_num_bytes_capacity, uint32_t* out_dst_num_bytes,
                           const char** out_error_msg) {
  assert(type);
  assert(src_bytes);
  assert(dst_bytes);
  assert(out_dst_num_bytes);
  assert(fidl::IsAligned(src_bytes));
  assert(fidl::IsAligned(dst_bytes));

  SrcDst src_dst(src_bytes, src_num_bytes, dst_bytes, dst_num_bytes_capacity, out_dst_num_bytes);
  switch (transformation) {
    case FIDL_TRANSFORMATION_NONE: {
      const auto start = Position{
          0, UINT16_MAX, /* unused: src_out_of_line_offset */
          0, UINT16_MAX, /* unused: dst_out_of_line_offset */
      };
      return src_dst.Copy(start, src_num_bytes);
    }
    case FIDL_TRANSFORMATION_V1_TO_OLD:
      return V1ToOld(&src_dst, out_error_msg).TransformTopLevelStruct(type);
    case FIDL_TRANSFORMATION_OLD_TO_V1:
      return OldToV1(&src_dst, out_error_msg).TransformTopLevelStruct(type);
    default: {
      if (out_error_msg)
        *out_error_msg = "unsupported transformation";
      return ZX_ERR_INVALID_ARGS;
    }
  }
}

#pragma GCC diagnostic pop  // "-Wimplicit-fallthrough"
