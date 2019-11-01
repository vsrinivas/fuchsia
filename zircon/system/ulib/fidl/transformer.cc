// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/internal.h>
#include <lib/fidl/transformer.h>

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
  uint32_t out_of_line_size = 0u;
  uint32_t handle_count = 0u;

  // TODO(apang): Can we delete this?
  TraversalResult& operator+=(const TraversalResult rhs) {
    out_of_line_size += rhs.out_of_line_size;
    handle_count += rhs.handle_count;

    return *this;
  }
};

// TODO(apang): I think we can get rid of the wire_format parameter by just doing union.alt.
// TODO(apang): This may not return the aligned size, since e.g. "type->coded_struct.size" below may
// be unaligned
uint32_t AlignedInlineSize(const fidl_type_t* type, WireFormat wire_format) {
  if (!type) {
    // For integral types (i.e. primitive, enum, bits).
    // TODO(apang): This returns the aligned size... but structs etc below don't return the aligned
    // size :/
    return 8;
  }
  switch (type->type_tag) {
    case fidl::kFidlTypePrimitive:
    case fidl::kFidlTypeEnum:
    case fidl::kFidlTypeBits:
      return 8;
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
      }
    case fidl::kFidlTypeArray:
      return type->coded_array.array_size;
    case fidl::kFidlTypeXUnion:
      return 24;
    case fidl::kFidlTypeHandle:
      return 8;
    case fidl::kFidlTypeTable:
      return 16;
  }

  // This is needed to suppress a GCC warning "control reaches end of non-void function", since GCC
  // treats switch() on enums as non-exhaustive without a default case.
  assert(false && "unexpected non-exhaustive switch on fidl::FidlTypeTag");
  return 0;
}

uint32_t AlignedAltInlineSize(const fidl_type_t* type) {
  if (!type) {
    // For integral types (i.e. primitive, enum, bits).
    return 8;
  }
  switch (type->type_tag) {
    case fidl::kFidlTypePrimitive:
    case fidl::kFidlTypeEnum:
    case fidl::kFidlTypeBits:
      return 8;
    case fidl::kFidlTypeStructPointer:
    case fidl::kFidlTypeUnionPointer:
      return 8;
    case fidl::kFidlTypeVector:
    case fidl::kFidlTypeString:
      return 16;
    case fidl::kFidlTypeStruct:
      return FIDL_ALIGN(type->coded_struct.alt_type->size);
    case fidl::kFidlTypeUnion:
      return FIDL_ALIGN(type->coded_union.alt_type->size);
    case fidl::kFidlTypeArray:
      return FIDL_ALIGN(type->coded_array.alt_type->array_size);
    case fidl::kFidlTypeXUnion:
      return 24;
    case fidl::kFidlTypeHandle:
      return 8;
    case fidl::kFidlTypeTable:
      return 16;
  }

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
    return Position(src_inline_offset + increase, src_out_of_line_offset, dst_inline_offset,
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
         uint32_t* out_dst_num_bytes)
      : src_bytes_(src_bytes),
        src_num_bytes_(src_num_bytes),
        dst_bytes_(dst_bytes),
        out_dst_num_bytes_(out_dst_num_bytes) {}
  SrcDst(const SrcDst&) = delete;

  ~SrcDst() { *out_dst_num_bytes_ = dst_max_offset_; }

  // TODO(apang): Change |position| arg to src_offset
  template <typename T>  // TODO(apang): restrict T should be pointer type
  const T* __attribute__((warn_unused_result)) Read(const Position& position) const {
    uint32_t size = sizeof(T);
    if (!(position.src_inline_offset + size <= src_num_bytes_)) {
      return nullptr;
    }

    return reinterpret_cast<const T*>(src_bytes_ + position.src_inline_offset);
  }

  // TODO(apang): Rename to CopyInline?
  void Copy(const Position& position, uint32_t size) {
    assert(position.src_inline_offset + size <= src_num_bytes_);

    memcpy(dst_bytes_ + position.dst_inline_offset, src_bytes_ + position.src_inline_offset, size);
    UpdateMaxOffset(position.dst_inline_offset + size);
  }

  // TODO(apang): Rename to PadInline
  void Pad(const Position& position, uint32_t size) {
    memset(dst_bytes_ + position.dst_inline_offset, 0, size);
    UpdateMaxOffset(position.dst_inline_offset + size);
  }

  void PadOutOfLine(const Position& position, uint32_t size) {
    memset(dst_bytes_ + position.dst_out_of_line_offset, 0, size);
    UpdateMaxOffset(position.dst_out_of_line_offset + size);
  }

  template <typename T>
  void Write(const Position& position, T value) {
    auto ptr = reinterpret_cast<T*>(dst_bytes_ + position.dst_inline_offset);
    *ptr = value;
    UpdateMaxOffset(position.dst_inline_offset + static_cast<uint32_t>(sizeof(value)));
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
    const auto start_position = Position(0, src_coded_struct.size, 0, dst_coded_struct.size);

    TraversalResult discarded_traversal_result;
    return TransformStruct(src_coded_struct, dst_coded_struct, start_position,
                           FIDL_ALIGN(dst_coded_struct.size), &discarded_traversal_result);
  }

 protected:
  zx_status_t Transform(const fidl_type_t* type, const Position& position, const uint32_t dst_size,
                        TraversalResult* out_traversal_result) {
    if (!type) {
      goto no_transform_just_copy;
    }

    switch (type->type_tag) {
      case fidl::kFidlTypeHandle: {
        auto presence = *src_dst->Read<uint32_t>(position);
        if (presence == FIDL_HANDLE_PRESENT) {
          out_traversal_result->handle_count++;
        }
      }
      case fidl::kFidlTypePrimitive:
      case fidl::kFidlTypeEnum:
      case fidl::kFidlTypeBits:
        goto no_transform_just_copy;
      case fidl::kFidlTypeStructPointer: {
        const auto& src_coded_struct = *type->coded_struct_pointer.struct_type;
        const auto& dst_coded_struct = *src_coded_struct.alt_type;
        return TransformStructPointer(src_coded_struct, dst_coded_struct, position,
                                      out_traversal_result);
      }
      case fidl::kFidlTypeUnionPointer:
        assert(false && "nullable unions are no longer supported");
        return ZX_ERR_BAD_STATE;
      case fidl::kFidlTypeStruct: {
        const auto& src_coded_struct = type->coded_struct;
        const auto& dst_coded_struct = *src_coded_struct.alt_type;
        return TransformStruct(src_coded_struct, dst_coded_struct, position, dst_size,
                               out_traversal_result);
      }
      case fidl::kFidlTypeUnion: {
        const auto& src_coded_union = type->coded_union;
        const auto& dst_coded_union = *src_coded_union.alt_type;
        return TransformUnion(src_coded_union, dst_coded_union, position, out_traversal_result);
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
        return TransformArray(src_coded_array, dst_coded_array, position, dst_array_size,
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
      default:
        return ZX_ERR_BAD_STATE;
    }

  no_transform_just_copy:  // TODO(apang): Remove goto here (can be inlined into case)
    src_dst->Copy(position, dst_size);

    // TODO(apang): Think about putting logic for updating out_traversal_result in Copy/etc
    // functions.

    return ZX_OK;
  }

  zx_status_t TransformStructPointer(const fidl::FidlCodedStruct& src_coded_struct,
                                     const fidl::FidlCodedStruct& dst_coded_struct,
                                     const Position& position,
                                     TraversalResult* out_traversal_result) {
    auto presence = *src_dst->Read<uint64_t>(position);
    src_dst->Copy(position, sizeof(uint64_t));

    if (presence != FIDL_ALLOC_PRESENT) {
      return ZX_OK;
    }

    uint32_t aligned_src_size = FIDL_ALIGN(src_coded_struct.size);
    uint32_t aligned_dst_size = FIDL_ALIGN(dst_coded_struct.size);
    const auto struct_position = Position{
        position.src_out_of_line_offset,
        position.src_out_of_line_offset + aligned_src_size,
        position.dst_out_of_line_offset,
        position.dst_out_of_line_offset + aligned_dst_size,
    };

    out_traversal_result->out_of_line_size += aligned_dst_size;

    return TransformStruct(src_coded_struct, dst_coded_struct, struct_position, aligned_dst_size,
                           out_traversal_result);
  }

  zx_status_t TransformStruct(const fidl::FidlCodedStruct& src_coded_struct,
                              const fidl::FidlCodedStruct& dst_coded_struct,
                              const Position& position, uint32_t dst_size,
                              TraversalResult* out_traversal_result) {
    // Note: we cannot use dst_coded_struct.size, and must instead rely on
    // the provided dst_size since this struct could be placed in an alignment
    // context that is larger than its inherent size.

    // Copy structs without any coded fields, and done.
    if (src_coded_struct.field_count == 0) {
      src_dst->Copy(position, dst_size);

      return ZX_OK;
    }

    const uint32_t src_start_of_struct = position.src_inline_offset;
    const uint32_t dst_start_of_struct = position.dst_inline_offset;
    const uint32_t dst_end_of_struct = position.dst_inline_offset + dst_size;

    auto current_position = position;

    // If the first field in the _coding table_ is non-primitive, there may be
    // primitive fields in the FIDL struct that are not present in the coding
    // table (which is an optimization, to save on coding table space). So,
    // copy everything before the first field if it's non-primitive.
    {
      const auto& src_first_field = src_coded_struct.fields[0];
      if (src_first_field.type != nullptr) {
        src_dst->Copy(position, src_first_field.offset);

        const auto& dst_first_field = *src_first_field.alt_field;
        const uint32_t dst_padding = dst_first_field.offset - src_first_field.offset;
        src_dst->Pad(position.IncreaseDstInlineOffset(src_first_field.offset), dst_padding);

        current_position = position.IncreaseSrcInlineOffset(src_first_field.offset)
                               .IncreaseDstInlineOffset(dst_first_field.offset);
      }
    }

    for (uint32_t src_field_index = 0; src_field_index < src_coded_struct.field_count;
         src_field_index++) {
      const auto& src_field = src_coded_struct.fields[src_field_index];

      // Copy fields without coding tables.
      if (!src_field.type) {
        const uint32_t dst_field_size =
            src_field.padding_offset + (src_start_of_struct - current_position.src_inline_offset);
        src_dst->Copy(current_position, dst_field_size);
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
          current_position.src_inline_offset + AlignedInlineSize(src_field.type, From());
      uint32_t dst_next_field_offset =
          current_position.dst_inline_offset + AlignedInlineSize(dst_field.type, To());
      uint32_t dst_field_size = dst_next_field_offset - dst_field.offset;

      const zx_status_t status =
          Transform(src_field.type, current_position, dst_field_size, out_traversal_result);
      if (status != ZX_OK) {
        return status;
      }

      // Update current position for next iteration.
      current_position.src_inline_offset = src_next_field_offset;
      current_position.dst_inline_offset = dst_next_field_offset;
    }

    // Copy everything after the last non-primitive field.
    // TODO(apang): Fix this
    if (From() == WireFormat::kOld) {
      uint32_t src_inline_remaining =
          position.src_inline_offset + src_coded_struct.size - current_position.src_inline_offset;
      src_dst->Copy(current_position, src_inline_remaining);
      current_position = current_position.IncreaseSrcInlineOffset(src_inline_remaining)
                             .IncreaseDstInlineOffset(src_inline_remaining);
    }

    // Pad end (if needed).
    if (current_position.dst_inline_offset < dst_end_of_struct) {
      uint32_t size = dst_end_of_struct - current_position.dst_inline_offset;
      src_dst->Pad(current_position, size);
    }

    return ZX_OK;
  }

  zx_status_t TransformVector(const fidl::FidlCodedVector& src_coded_vector,
                              const fidl::FidlCodedVector& dst_coded_vector,
                              const Position& position, TraversalResult* out_traversal_result) {
    const auto& src_vector = *src_dst->Read<fidl_vector_t>(position);

    // Copy vector header.
    src_dst->Copy(position, sizeof(fidl_vector_t));

    // Early exit on nullable vectors.  // TODO(apang): Switch() against FIDL_ALLOC_PRESENT and
    // ABSENT (and eveyrwhere elese)
    auto presence = reinterpret_cast<uint64_t>(src_vector.data);
    if (presence != FIDL_ALLOC_PRESENT) {
      return ZX_OK;
    }

    const auto convert = [&](const fidl::FidlCodedVector& coded_vector) {
      return fidl::FidlCodedArrayNew(coded_vector.element, src_vector.count,
                                     coded_vector.element_size, 0,
                                     nullptr /* alt_type unused, we provide both src and dst */);
    };
    const auto src_vector_data_as_coded_array = convert(src_coded_vector);
    const auto dst_vector_data_as_coded_array = convert(dst_coded_vector);

    // Calculate vector size. They fit in uint32_t due to automatic bounds.
    uint32_t src_vector_size =
        FIDL_ALIGN(static_cast<uint32_t>(src_vector.count * (src_coded_vector.element_size)));
    uint32_t dst_vector_size =
        FIDL_ALIGN(static_cast<uint32_t>(src_vector.count * (dst_coded_vector.element_size)));

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

    out_traversal_result->out_of_line_size += dst_vector_size;

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

    if (src_envelope->presence == FIDL_ALLOC_ABSENT) {
      src_dst->Copy(position, sizeof(fidl_envelope_t));
      return ZX_OK;
    }

    if (!known_type) {
      // Unknown type, so we don't know what type of data the envelope contains.
      src_dst->Copy(Position{position.src_out_of_line_offset,
                             position.src_out_of_line_offset + src_envelope->num_bytes,
                             position.dst_out_of_line_offset,
                             position.dst_out_of_line_offset + src_envelope->num_bytes},
                    src_envelope->num_bytes);
      return ZX_OK;
    }

    const uint32_t alt_field_size = AlignedAltInlineSize(type);
    Position data_position =
        Position{position.src_out_of_line_offset,
                 position.src_out_of_line_offset + FIDL_ALIGN(AlignedInlineSize(type, From())),
                 position.dst_out_of_line_offset, position.dst_out_of_line_offset + alt_field_size};
    TraversalResult contents_traversal_result;
    zx_status_t result = Transform(type, data_position, alt_field_size, &contents_traversal_result);
    if (result != ZX_OK) {
      return result;
    }

    const uint32_t dst_contents_size = alt_field_size + contents_traversal_result.out_of_line_size;

    fidl_envelope_t dst_envelope = *src_envelope;
    dst_envelope.num_bytes = dst_contents_size;
    src_dst->Write(position, dst_envelope);

    out_traversal_result->out_of_line_size += dst_contents_size;
    out_traversal_result->handle_count += src_envelope->num_handles;

    return ZX_OK;
  }

  zx_status_t TransformXUnion(const fidl::FidlCodedXUnion& coded_xunion, const Position& position,
                              TraversalResult* out_traversal_result) {
    auto xunion = src_dst->Read<const fidl_xunion_t>(position);
    src_dst->Copy(position, sizeof(fidl_xunion_t));

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
    src_dst->Copy(position, sizeof(fidl_table_t));

    const Position envelopes_vector_position = position.IncreaseInlineOffset(sizeof(fidl_table_t));
    const fidl_envelope_t* envelopes_vector =
        src_dst->Read<fidl_envelope_t>(envelopes_vector_position);
    const uint32_t envelopes_vector_size =
        static_cast<uint32_t>(table->envelopes.count * sizeof(fidl_envelope_t));
    src_dst->Copy(envelopes_vector_position, envelopes_vector_size);

    uint32_t src_envelope_data_offset = envelopes_vector_size;
    uint32_t dst_envelope_data_offset = src_envelope_data_offset;

    for (uint32_t i = 0, field_index = 0; i < table->envelopes.count; i++) {
      const fidl::FidlTableField& field = coded_table.fields[field_index];

      // TODO(apang): Comment about why i+1 below.
      if (i + 1 < field.ordinal) {
        // This coded_table has some reserved fields before the first
        // non-reserved field. The vector<envelope> includes all
        // fields--including reserved fields--so skip reserved fields in the
        // vector<envelope>.
        continue;
      }

      field_index++;

      assert(field.ordinal == i + 1);

      // TODO(apang): De-dupe below.
      auto envelope_position = Position{
          position.src_out_of_line_offset + i * static_cast<uint32_t>(sizeof(fidl_envelope_t)),
          position.src_out_of_line_offset + src_envelope_data_offset,
          position.dst_out_of_line_offset + i * static_cast<uint32_t>(sizeof(fidl_envelope_t)),
          position.dst_out_of_line_offset + dst_envelope_data_offset,
      };

      TraversalResult envelope_traversal_result;
      zx_status_t status =
          TransformEnvelope(true, field.type, envelope_position, &envelope_traversal_result);

      if (status != ZX_OK) {
        return status;
      }

      src_envelope_data_offset += envelopes_vector[i].num_bytes;
      dst_envelope_data_offset += envelope_traversal_result.out_of_line_size;

      // TODO(apang): Add operator+ to TraversalResult?
      out_traversal_result->out_of_line_size += envelope_traversal_result.out_of_line_size;
      out_traversal_result->handle_count += envelope_traversal_result.handle_count;
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
      src_dst->Copy(position, dst_array_size);
      return ZX_OK;
    }

    // Slow path otherwise.
    auto current_element_position = position;
    for (uint32_t i = 0; i < src_coded_array.element_count; i++) {
      const zx_status_t status = Transform(src_coded_array.element, current_element_position,
                                           dst_coded_array.element_size, out_traversal_result);
      if (status != ZX_OK) {
        return status;
      }

      // Pad end of an element.
      auto padding_position =
          current_element_position.IncreaseSrcInlineOffset(src_coded_array.element_size)
              .IncreaseDstInlineOffset(dst_coded_array.element_size);
      src_dst->Pad(padding_position, dst_coded_array.element_padding);

      current_element_position =
          padding_position.IncreaseSrcInlineOffset(src_coded_array.element_padding)
              .IncreaseDstInlineOffset(dst_coded_array.element_padding);
    }

    // Pad end of elements.
    uint32_t padding =
        dst_array_size + position.dst_inline_offset - current_element_position.dst_inline_offset;
    src_dst->Pad(current_element_position, padding);

    return ZX_OK;
  }

  virtual WireFormat From() const = 0;
  virtual WireFormat To() const = 0;

  virtual zx_status_t TransformUnion(const fidl::FidlCodedUnion& src_coded_union,
                                     const fidl::FidlCodedUnion& dst_coded_union,
                                     const Position& position,
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

  zx_status_t TransformUnion(const fidl::FidlCodedUnion& src_coded_union,
                             const fidl::FidlCodedUnion& dst_coded_union, const Position& position,
                             TraversalResult* out_traversal_result) {
    assert(src_coded_union.field_count == dst_coded_union.field_count);

    // Read: extensible-union ordinal.
    auto src_xunion = src_dst->Read<const fidl_xunion_t>(position);
    uint32_t xunion_ordinal = src_xunion->tag;

    if (src_xunion->padding != static_cast<decltype(src_xunion->padding)>(0)) {
      return Fail(ZX_ERR_BAD_STATE, "xunion padding is non-zero");
    }

    // TODO(apang): Can probably remove this validation here, the walker will validate it for us.
    switch (src_xunion->envelope.presence) {
      case FIDL_ALLOC_PRESENT:
        // OK
        break;
      case FIDL_ALLOC_ABSENT:
        return Fail(ZX_ERR_BAD_STATE, "xunion envelope is invalid FIDL_ALLOC_ABSENT");
      default:
        return Fail(ZX_ERR_BAD_STATE,
                    "xunion envelope presence neither FIDL_ALLOC_PRESENT nor FIDL_ALLOC_ABSENT");
    }

    // Retrieve: flexible-union field (or variant).
    bool src_field_found = false;
    uint32_t src_field_index = 0;
    const fidl::FidlUnionField* src_field = nullptr;
    for (/* src_field_index needed after the loop */; src_field_index < src_coded_union.field_count;
         src_field_index++) {
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

    // Transform: xunion field to static-union field (or variant).
    auto field_position = Position{
        position.src_out_of_line_offset,
        position.src_out_of_line_offset + AlignedInlineSize(src_field->type, WireFormat::kOld),
        position.dst_inline_offset + dst_coded_union.data_offset,
        position.dst_out_of_line_offset,
    };
    uint32_t dst_field_size = dst_coded_union.size - dst_coded_union.data_offset;

    zx_status_t status =
        Transform(src_field->type, field_position, dst_field_size, out_traversal_result);
    if (status != ZX_OK) {
      return status;
    }

    // Pad after static-union data.
    auto field_padding_position =
        field_position.IncreaseDstInlineOffset(dst_field_size - dst_field.padding);
    src_dst->Pad(field_padding_position, dst_field.padding);

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

  zx_status_t TransformUnion(const fidl::FidlCodedUnion& src_coded_union,
                             const fidl::FidlCodedUnion& dst_coded_union, const Position& position,
                             TraversalResult* out_traversal_result) {
    assert(src_coded_union.field_count == dst_coded_union.field_count);

    // Read: union tag.
    const fidl_union_tag_t union_tag = *src_dst->Read<const fidl_union_tag_t>(position);

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

    TraversalResult traversal_result;
    zx_status_t status =
        Transform(src_field.type, field_position, dst_inline_field_size, &traversal_result);
    if (status != ZX_OK) {
      return status;
    }

    const uint32_t dst_field_size = dst_inline_field_size + traversal_result.out_of_line_size;

    fidl_xunion_t xunion;
    xunion.tag = dst_field.xunion_ordinal;
    xunion.padding = 0;
    xunion.envelope.num_bytes = FIDL_ALIGN(dst_field_size);
    xunion.envelope.num_handles = traversal_result.handle_count;
    xunion.envelope.presence = FIDL_ALLOC_PRESENT;
    src_dst->Write(position, xunion);

    // Pad xunion data to object alignment.
    const uint32_t dst_padding = FIDL_ALIGN(dst_field_size) - dst_field_size;
    src_dst->PadOutOfLine(position.IncreaseDstOutOfLineOffset(dst_field_size), dst_padding);

    out_traversal_result->out_of_line_size += FIDL_ALIGN(dst_field_size);

    return ZX_OK;
  }
};

}  // namespace

zx_status_t fidl_transform(fidl_transformation_t transformation, const fidl_type_t* type,
                           const uint8_t* src_bytes, uint32_t src_num_bytes, uint8_t* dst_bytes,
                           uint32_t* out_dst_num_bytes, const char** out_error_msg) {
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
