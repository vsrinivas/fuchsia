// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/coding.h>
#include <lib/fidl/internal.h>
#include <lib/fidl/transformer.h>

#include <cassert>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <string>

// Disable warning about implicit fallthrough, since it's intentionally used a
// lot in this code, and the switch()es end up being harder to read without it.
// Note that "#pragma GCC" works for both GCC & Clang.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"

namespace {

// This is an array of 32-bit ordinals that's intended to help debugging. The
// array is normally empty, but you can add an ordinal to this array in your
// local tree if you encounter a message in-the-field that the transformer is
// having issues with.
constexpr uint64_t kDebugOrdinals[] = {
    // 0x61f19458'00000000,  // example ordinal
};

enum struct WireFormat {
  kOld,
  kV1,
};

// Call this macro instead of assert() when inside a TransformerBase method: it
// will print out useful debugging information. This macro inlines the code in
// order to get precise line number information, and to know the assertion being
// evaluated.
#define TRANSFORMER_ASSERT(assertion, position)                       \
  {                                                                   \
    const auto ok = static_cast<bool>(assertion);                     \
    if (!ok) {                                                        \
      debug_info_->RecordFailure(__LINE__, (#assertion), (position)); \
      assert(assertion);                                              \
    }                                                                 \
  }

// Call this macro instead of the TransformerBase::Fail() method. This is a simple wrapper to pass
// the current __LINE__ number to Fail().
#define TRANSFORMER_FAIL(status, position, error_message) \
  Fail(status, position, __LINE__, error_message);

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

constexpr uint32_t PrimitiveSize(const FidlCodedPrimitive primitive) {
  switch (primitive) {
    case kFidlCodedPrimitive_Bool:
    case kFidlCodedPrimitive_Int8:
    case kFidlCodedPrimitive_Uint8:
      return 1;
    case kFidlCodedPrimitive_Int16:
    case kFidlCodedPrimitive_Uint16:
      return 2;
    case kFidlCodedPrimitive_Int32:
    case kFidlCodedPrimitive_Uint32:
    case kFidlCodedPrimitive_Float32:
      return 4;
    case kFidlCodedPrimitive_Int64:
    case kFidlCodedPrimitive_Uint64:
    case kFidlCodedPrimitive_Float64:
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
    case kFidlTypePrimitive:
      return PrimitiveSize(type->coded_primitive);
    case kFidlTypeEnum:
      return PrimitiveSize(type->coded_enum.underlying_type);
    case kFidlTypeBits:
      return PrimitiveSize(type->coded_bits.underlying_type);
    case kFidlTypeStructPointer:
      return 8;
    case kFidlTypeUnionPointer:
      assert(wire_format == WireFormat::kOld);
      return 8;
    case kFidlTypeVector:
    case kFidlTypeString:
      return 16;
    case kFidlTypeStruct:
      return type->coded_struct.size;
    case kFidlTypeUnion:
      assert(wire_format == WireFormat::kOld);
      return type->coded_union.size;
    case kFidlTypeArray:
      return type->coded_array.array_size;
    case kFidlTypeXUnion:
      return 24;
    case kFidlTypeHandle:
      return 4;
    case kFidlTypeTable:
      return 16;
  }

  // This is needed to suppress a GCC warning "control reaches end of non-void function", since GCC
  // treats switch() on enums as non-exhaustive without a default case.
  assert(false && "unexpected non-exhaustive switch on FidlTypeTag");
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

  Position(const Position&) = default;
  Position& operator=(const Position&) = default;

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

  std::string ToString() const {
    char buffer[32];

    snprintf(buffer, sizeof(buffer), "{0x%02x, 0x%02x, 0x%02x, 0x%02x}", src_inline_offset,
             src_out_of_line_offset, dst_inline_offset, dst_out_of_line_offset);
    return std::string(buffer);
  }
};

class SrcDst final {
 public:
  SrcDst(const uint8_t* src_bytes, const uint32_t src_num_bytes, uint8_t* dst_bytes,
         uint32_t dst_num_bytes_capacity)
      : src_bytes_(src_bytes),
        src_max_offset_(src_num_bytes),
        dst_bytes_(dst_bytes),
        dst_max_offset_(dst_num_bytes_capacity) {}
  SrcDst(const SrcDst&) = delete;

  // Reads |T| from |src_bytes|.
  // This may update the max src read offset if needed.
  template <typename T>
  const T* __attribute__((warn_unused_result)) Read(const Position& position) {
    return Read<T>(position, sizeof(T));
  }

  // Reads |size| bytes from |src_bytes|, but only returns a pointer to |T|
  // which may be smaller, i.e. |sizeof(T)| can be smaller than |size|.
  // This may update the max src read offset if needed.
  template <typename T>
  const T* __attribute__((warn_unused_result)) Read(const Position& position, uint32_t size) {
    assert(sizeof(T) <= size);
    const auto status = src_max_offset_.Update(position.src_inline_offset + size);
    if (status != ZX_OK) {
      return nullptr;
    }

    return reinterpret_cast<const T*>(src_bytes_ + position.src_inline_offset);
  }

  // Copies |size| bytes from |src_bytes| to |dst_bytes|.
  // This may update the max src read offset if needed.
  // This may update the max dst written offset if needed.
  zx_status_t __attribute__((warn_unused_result)) Copy(const Position& position, uint32_t size) {
    const auto src_status = src_max_offset_.Update(position.src_inline_offset + size);
    if (src_status != ZX_OK) {
      return src_status;
    }
    const auto dst_status = dst_max_offset_.Update(position.dst_inline_offset + size);
    if (dst_status != ZX_OK) {
      return dst_status;
    }

    memcpy(dst_bytes_ + position.dst_inline_offset, src_bytes_ + position.src_inline_offset, size);
    return ZX_OK;
  }

  // Pads |size| bytes in |dst_bytes|.
  // This may update the max dst written offset if needed.
  zx_status_t __attribute__((warn_unused_result)) Pad(const Position& position, uint32_t size) {
    const auto status = dst_max_offset_.Update(position.dst_inline_offset + size);
    if (status != ZX_OK) {
      return status;
    }
    memset(dst_bytes_ + position.dst_inline_offset, 0, size);
    return ZX_OK;
  }

  // Writes |value| in |dst_bytes|.
  // This may update the max dst written offset if needed.
  template <typename T>
  zx_status_t __attribute__((warn_unused_result)) Write(const Position& position, T value) {
    const auto size = static_cast<uint32_t>(sizeof(value));
    const auto status = dst_max_offset_.Update(position.dst_inline_offset + size);
    if (status != ZX_OK) {
      return status;
    }
    auto ptr = reinterpret_cast<T*>(dst_bytes_ + position.dst_inline_offset);
    *ptr = value;
    return ZX_OK;
  }

  const uint8_t* src_bytes() const { return src_bytes_; }
  uint32_t src_num_bytes() const { return src_max_offset_.capacity_; }
  uint32_t src_max_offset_read() const { return src_max_offset_.max_offset_; }

  uint8_t* dst_bytes() const { return dst_bytes_; }
  uint32_t dst_num_bytes_capacity() const { return dst_max_offset_.capacity_; }
  uint32_t dst_max_offset_written() const { return dst_max_offset_.max_offset_; }

 private:
  struct MaxOffset {
    MaxOffset(uint32_t capacity) : capacity_(capacity) {}
    const uint32_t capacity_;
    uint32_t max_offset_ = 0;

    zx_status_t __attribute__((warn_unused_result)) Update(uint32_t offset) {
      if (offset > capacity_) {
        return ZX_ERR_BAD_STATE;
      }
      if (offset > max_offset_) {
        max_offset_ = offset;
      }
      return ZX_OK;
    }
  };

  const uint8_t* src_bytes_;
  MaxOffset src_max_offset_;
  uint8_t* dst_bytes_;
  MaxOffset dst_max_offset_;
};

// Debug related information, which is set both on construction, and as we
// transform. On destruction, this object writes any collected error message
// if an |out_error_msg| is provided.
class DebugInfo final {
 public:
  DebugInfo(fidl_transformation_t transformation, const fidl_type_t* type, const SrcDst& src_dst,
            const char** out_error_msg)
      : transformation_(transformation),
        type_(type),
        src_dst_(src_dst),
        out_error_msg_(out_error_msg) {}
  DebugInfo(const DebugInfo&) = delete;

  ~DebugInfo() {
    if (has_failed_) {
      Print("ERROR");
    }
    if (out_error_msg_) {
      *out_error_msg_ = error_msg_;
    }
  }

  void RecordFailure(int line_number, const char* error_msg) {
    has_failed_ = true;
    error_msg_ = error_msg;
    line_number_ = line_number;
  }

  void RecordFailure(int line_number, const char* error_msg, const Position& position) {
    RecordFailure(line_number, error_msg);
    position_ = position;
  }

  void DebugPrint(int line_number, const char* error_msg, const Position& position) const {
    DebugInfo dup(transformation_, type_, src_dst_, nullptr);
    dup.RecordFailure(line_number, error_msg, position);
    dup.Print("INFO");
    // Avoid printing twice.
    dup.has_failed_ = false;
  }

 private:
  void Print(const std::string& failure_type) const {
    printf("=== TRANSFORMER %s ===\n", failure_type.c_str());

    char type_desc[256] = {};
    fidl_format_type_name(type_, type_desc, sizeof(type_desc));

    printf("src: " __FILE__ "\n");
    printf("direction: %s\n", direction().c_str());
    printf("transformer.cc:%d: %s\n", line_number_, error_msg_);
    printf("top level type: %s\n", type_desc);
    printf("position: %s\n", position_.ToString().c_str());

    auto print_bytes = [&](const uint8_t* buffer, uint32_t size, uint32_t out_of_line_offset) {
      for (uint32_t i = 0; i < size; i++) {
        if (i == out_of_line_offset) {
          printf("  // out-of-line\n");
        }

        if (i % 8 == 0) {
          printf("  ");
        }

        printf("0x%02x, ", buffer[i]);

        if (i % 0x10 == 0x07) {
          printf("  // 0x%02x\n", i - 7);
        } else if (i % 0x08 == 0x07) {
          printf("\n");
        }
      }
    };

    printf("uint8_t src_bytes[0x%02x] = {\n", src_dst_.src_num_bytes());
    print_bytes(src_dst_.src_bytes(), src_dst_.src_num_bytes(), position_.src_out_of_line_offset);
    printf("}\n");

    printf("uint8_t dst_bytes[0x%02x] = {  // capacity = 0x%02x\n",
           src_dst_.dst_max_offset_written(), src_dst_.dst_num_bytes_capacity());
    print_bytes(src_dst_.dst_bytes(), src_dst_.dst_max_offset_written(),
                position_.dst_out_of_line_offset);
    printf("}\n");

    printf("=== END TRANSFORMER %s ===\n", failure_type.c_str());
  }

  std::string direction() const {
    switch (transformation_) {
      case FIDL_TRANSFORMATION_NONE:
        return "none";
      case FIDL_TRANSFORMATION_V1_TO_OLD:
        return "v1 to old";
      case FIDL_TRANSFORMATION_OLD_TO_V1:
        return "old to v1";
      default:
        return "unknown";
    }
  }

  // Set on construction, and duplicated.
  const fidl_transformation_t transformation_;
  const fidl_type_t* type_;
  const SrcDst& src_dst_;

  // Set on construction, never duplicated.
  const char** out_error_msg_;

  // Set post construction, never duplicated.
  bool has_failed_ = false;
  const char* error_msg_ = nullptr;
  int line_number_ = -1;
  Position position_{0, 0, 0, 0};
};

class TransformerBase {
 public:
  TransformerBase(SrcDst* src_dst, const fidl_type_t* top_level_type, DebugInfo* debug_info)
      : src_dst(src_dst), debug_info_(debug_info), top_level_type_(top_level_type) {}
  virtual ~TransformerBase() = default;

  uint32_t InlineSize(const fidl_type_t* type, WireFormat wire_format, const Position& position) {
    TRANSFORMER_ASSERT(type, position);

    return UnsafeInlineSize(type, wire_format);
  }

  uint32_t AltInlineSize(const fidl_type_t* type, const Position& position) {
    TRANSFORMER_ASSERT(type, position);

    switch (type->type_tag) {
      case kFidlTypeStruct:
        return InlineSize(type->coded_struct.alt_type, To(), position);
      case kFidlTypeUnion:
        return InlineSize(type->coded_union.alt_type, To(), position);
      case kFidlTypeArray:
        return InlineSize(type->coded_array.alt_type, To(), position);
      case kFidlTypeXUnion:
        return InlineSize(type->coded_xunion.alt_type, To(), position);
      case kFidlTypePrimitive:
      case kFidlTypeEnum:
      case kFidlTypeBits:
      case kFidlTypeStructPointer:
      case kFidlTypeUnionPointer:
      case kFidlTypeVector:
      case kFidlTypeString:
      case kFidlTypeHandle:
      case kFidlTypeTable:
        return InlineSize(type, To(), position);
    }

    TRANSFORMER_ASSERT(false && "unexpected non-exhaustive switch on FidlTypeTag", position);
    return 0;
  }

  void MaybeDebugPrintTopLevelStruct(const Position& position) {
    if (sizeof(kDebugOrdinals) == 0) {
      return;
    }

    auto maybe_ordinal = src_dst->Read<uint64_t>(
        position.IncreaseSrcInlineOffset(offsetof(fidl_message_header_t, ordinal)));
    if (!maybe_ordinal) {
      return;
    }

    for (uint64_t debug_ordinal : kDebugOrdinals) {
      if (debug_ordinal != *maybe_ordinal) {
        continue;
      }

      char buffer[16];
      snprintf(buffer, sizeof(buffer), "0x%016" PRIx64, debug_ordinal);

      debug_info_->DebugPrint(__LINE__, buffer, position);
    }
  }

  zx_status_t TransformTopLevelStruct() {
    if (top_level_type_->type_tag != kFidlTypeStruct) {
      return TRANSFORMER_FAIL(ZX_ERR_INVALID_ARGS, (Position{0, 0, 0, 0}),
                              "only top-level structs supported");
    }

    const auto& src_coded_struct = top_level_type_->coded_struct;
    const auto& dst_coded_struct = src_coded_struct.alt_type->coded_struct;
    // Since this is the top-level struct, the first secondary object (i.e.
    // out-of-line offset) is exactly placed after this struct, i.e. the
    // struct's inline size.
    const auto start_position = Position(0, src_coded_struct.size, 0, dst_coded_struct.size);

    TraversalResult discarded_traversal_result;
    const zx_status_t status =
        TransformStruct(src_coded_struct, dst_coded_struct, start_position,
                        FIDL_ALIGN(dst_coded_struct.size), &discarded_traversal_result);
    MaybeDebugPrintTopLevelStruct(start_position);
    return status;
  }

 protected:
  zx_status_t Transform(const fidl_type_t* type, const Position& position, const uint32_t dst_size,
                        TraversalResult* out_traversal_result) {
    if (!type) {
      return src_dst->Copy(position, dst_size);
    }
    switch (type->type_tag) {
      case kFidlTypeHandle:
        return TransformHandle(position, dst_size, out_traversal_result);
      case kFidlTypePrimitive:
      case kFidlTypeEnum:
      case kFidlTypeBits:
        return src_dst->Copy(position, dst_size);
      case kFidlTypeStructPointer: {
        const auto& src_coded_struct = *type->coded_struct_pointer.struct_type;
        const auto& dst_coded_struct = src_coded_struct.alt_type->coded_struct;
        return TransformStructPointer(src_coded_struct, dst_coded_struct, position,
                                      out_traversal_result);
      }
      case kFidlTypeUnionPointer: {
        const auto& src_coded_union = *type->coded_union_pointer.union_type;
        const auto& dst_coded_xunion = src_coded_union.alt_type->coded_xunion;
        return TransformUnionPointerToOptionalXUnion(src_coded_union, dst_coded_xunion, position,
                                                     out_traversal_result);
      }
      case kFidlTypeStruct: {
        const auto& src_coded_struct = type->coded_struct;
        const auto& dst_coded_struct = src_coded_struct.alt_type->coded_struct;
        return TransformStruct(src_coded_struct, dst_coded_struct, position, dst_size,
                               out_traversal_result);
      }
      case kFidlTypeUnion: {
        const auto& src_coded_union = type->coded_union;
        const auto& dst_coded_union = src_coded_union.alt_type->coded_xunion;
        return TransformUnionToXUnion(src_coded_union, dst_coded_union, position, dst_size,
                                      out_traversal_result);
      }
      case kFidlTypeArray: {
        const auto convert = [](const FidlCodedArray& coded_array) {
          FidlCodedArrayNew result = {
              .element = coded_array.element,
              .element_count = coded_array.array_size / coded_array.element_size,
              .element_size = coded_array.element_size,
              .element_padding = 0,
              .alt_type = nullptr /* alt_type unused, we provide both src and dst */};
          return result;
        };
        auto src_coded_array = convert(type->coded_array);
        auto dst_coded_array = convert(type->coded_array.alt_type->coded_array);
        return TransformArray(src_coded_array, dst_coded_array, position, dst_size,
                              out_traversal_result);
      }
      case kFidlTypeString:
        return TransformString(position, out_traversal_result);
      case kFidlTypeVector: {
        const auto& src_coded_vector = type->coded_vector;
        const auto& dst_coded_vector = src_coded_vector.alt_type->coded_vector;
        return TransformVector(src_coded_vector, dst_coded_vector, position, out_traversal_result);
      }
      case kFidlTypeTable:
        return TransformTable(type->coded_table, position, out_traversal_result);
      case kFidlTypeXUnion:
        TRANSFORMER_ASSERT(type->coded_xunion.alt_type, position);

        switch (type->coded_xunion.alt_type->type_tag) {
          case kFidlTypeUnion:
            return TransformXUnionToUnion(type->coded_xunion,
                                          type->coded_xunion.alt_type->coded_union, position,
                                          dst_size, out_traversal_result);
          case kFidlTypeUnionPointer:
            return TransformOptionalXUnionToUnionPointer(
                type->coded_xunion, *type->coded_xunion.alt_type->coded_union_pointer.union_type,
                position, out_traversal_result);
          case kFidlTypeXUnion:
            return TransformXUnion(type->coded_xunion, position, out_traversal_result);
          case kFidlTypePrimitive:
          case kFidlTypeEnum:
          case kFidlTypeBits:
          case kFidlTypeStruct:
          case kFidlTypeStructPointer:
          case kFidlTypeArray:
          case kFidlTypeString:
          case kFidlTypeHandle:
          case kFidlTypeVector:
          case kFidlTypeTable:
            TRANSFORMER_ASSERT(false && "Invalid src_xunion alt_type->type_tag", position);
            __builtin_unreachable();
        }
    }

    return TRANSFORMER_FAIL(ZX_ERR_BAD_STATE, position, "unknown type tag");

    // TODO(apang): Think about putting logic for updating out_traversal_result in Copy/etc
    // functions.
  }

  zx_status_t TransformHandle(const Position& position, uint32_t dst_size,
                              TraversalResult* out_traversal_result) {
    auto presence = src_dst->Read<uint32_t>(position);
    if (!presence) {
      return TRANSFORMER_FAIL(ZX_ERR_BAD_STATE, position, "handle presence missing");
    }
    switch (*presence) {
      case FIDL_HANDLE_ABSENT:
        // Ok
        break;
      case FIDL_HANDLE_PRESENT:
        out_traversal_result->handle_count++;
        break;
      default:
        return TRANSFORMER_FAIL(ZX_ERR_BAD_STATE, position, "handle presence invalid");
    }
    return src_dst->Copy(position, dst_size);
  }

  zx_status_t TransformStructPointer(const FidlCodedStruct& src_coded_struct,
                                     const FidlCodedStruct& dst_coded_struct,
                                     const Position& position,
                                     TraversalResult* out_traversal_result) {
    auto presence = src_dst->Read<uint64_t>(position);
    if (!presence) {
      return TRANSFORMER_FAIL(ZX_ERR_BAD_STATE, position, "struct pointer missing");
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
        return TRANSFORMER_FAIL(ZX_ERR_BAD_STATE, position, "struct pointer invalid");
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

  zx_status_t TransformStruct(const FidlCodedStruct& src_coded_struct,
                              const FidlCodedStruct& dst_coded_struct, const Position& position,
                              uint32_t dst_size, TraversalResult* out_traversal_result) {
    TRANSFORMER_ASSERT(src_coded_struct.field_count == dst_coded_struct.field_count, position);
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
          TRANSFORMER_ASSERT(src_field.offset == dst_field.offset, current_position);
          const auto status_copy_field = src_dst->Copy(current_position, src_field.offset);
          if (status_copy_field != ZX_OK) {
            return status_copy_field;
          }
          current_position = current_position.IncreaseInlineOffset(src_field.offset);
        }

        TRANSFORMER_ASSERT(
            current_position.src_inline_offset == src_start_of_struct + src_field.offset,
            current_position);
        TRANSFORMER_ASSERT(
            current_position.dst_inline_offset == dst_start_of_struct + dst_field.offset,
            current_position);

        // Transform field.
        uint32_t src_next_field_offset = current_position.src_inline_offset +
                                         InlineSize(src_field.type, From(), current_position);
        uint32_t dst_next_field_offset =
            current_position.dst_inline_offset + InlineSize(dst_field.type, To(), current_position);
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
        return TRANSFORMER_FAIL(status_pad, current_position,
                                "unable to pad end of struct element");
      }
      current_position = current_position.IncreaseDstInlineOffset(dst_field.padding);
      current_position = current_position.IncreaseSrcInlineOffset(src_field.padding);
    }

    // Pad (possibly with 0 bytes).
    const uint32_t dst_end_of_struct = position.dst_inline_offset + dst_size;
    const auto status_pad =
        src_dst->Pad(current_position, dst_end_of_struct - current_position.dst_inline_offset);
    if (status_pad != ZX_OK) {
      return TRANSFORMER_FAIL(status_pad, current_position, "unable to pad end of struct");
    }

    return ZX_OK;
  }

  zx_status_t TransformVector(const FidlCodedVector& src_coded_vector,
                              const FidlCodedVector& dst_coded_vector, const Position& position,
                              TraversalResult* out_traversal_result) {
    const auto src_vector = src_dst->Read<fidl_vector_t>(position);
    if (!src_vector) {
      return TRANSFORMER_FAIL(ZX_ERR_BAD_STATE, position, "vector missing");
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
        return TRANSFORMER_FAIL(ZX_ERR_BAD_STATE, position, "vector presence invalid");
    }

    const auto convert = [&](const FidlCodedVector& coded_vector) {
      FidlCodedArrayNew result = {
          .element = coded_vector.element,
          .element_count = static_cast<uint32_t>(src_vector->count),
          .element_size = coded_vector.element_size,
          .element_padding = 0,
          .alt_type = nullptr /* alt_type unused, we provide both src and dst */};
      return result;
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
    FidlCodedVector string_as_coded_vector = {
        .element = nullptr,
        .max_count = 0 /*unused*/,
        .element_size = 1,
        .nullable = kFidlNullability_Nullable, /* constraints are not checked, i.e. unused */
        nullptr /* alt_type unused, we provide both src and dst */};
    return TransformVector(string_as_coded_vector, string_as_coded_vector, position,
                           out_traversal_result);
  }

  // TODO(apang): Replace |known_type| & |type| parameters below with a single fit::optional<const
  // fidl_type_t*> parameter.
  zx_status_t TransformEnvelope(bool known_type, const fidl_type_t* type, const Position& position,
                                TraversalResult* out_traversal_result) {
    auto src_envelope = src_dst->Read<const fidl_envelope_t>(position);
    if (!src_envelope) {
      return TRANSFORMER_FAIL(ZX_ERR_BAD_STATE, position, "envelope missing");
    }

    switch (src_envelope->presence) {
      case FIDL_ALLOC_ABSENT: {
        const auto status = src_dst->Copy(position, sizeof(fidl_envelope_t));
        if (status != ZX_OK) {
          return TRANSFORMER_FAIL(status, position, "unable to copy envelope header");
        }
        return ZX_OK;
      }
      case FIDL_ALLOC_PRESENT:
        // We write the transformed envelope after the transformation, since
        // the num_bytes may be different in the dst.
        break;
      default:
        return TRANSFORMER_FAIL(ZX_ERR_BAD_STATE, position, "envelope presence invalid");
    }

    if (!known_type) {
      // When we encounter an unknown type, the best we can do is to copy the
      // envelope header (which includes the num_bytes and num_handles), and
      // copy the envelope's data. While it's possible that transformation was
      // needed, since we do not have the type, we cannot perform it.

      const auto status_copy_hdr = src_dst->Copy(position, sizeof(fidl_envelope_t));
      if (status_copy_hdr != ZX_OK) {
        return TRANSFORMER_FAIL(status_copy_hdr, position,
                                "unable to copy envelope header (unknown type)");
      }

      const auto data_position =
          Position{position.src_out_of_line_offset,
                   position.src_out_of_line_offset + src_envelope->num_bytes,
                   position.dst_out_of_line_offset,
                   position.dst_out_of_line_offset + src_envelope->num_bytes};
      const auto status_copy_data = src_dst->Copy(data_position, src_envelope->num_bytes);
      if (status_copy_data != ZX_OK) {
        return TRANSFORMER_FAIL(status_copy_data, data_position,
                                "unable to copy envelope data (unknown type)");
      }

      out_traversal_result->src_out_of_line_size += src_envelope->num_bytes;
      out_traversal_result->dst_out_of_line_size += src_envelope->num_bytes;
      out_traversal_result->handle_count += src_envelope->num_handles;

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

      // TODO(apang): Add test to verify that we should _not_ FIDL_ALIGN below.
      return InlineSize(type, From(), position);
    }();

    const uint32_t dst_contents_inline_size = FIDL_ALIGN(AltInlineSize(type, position));
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
      return TRANSFORMER_FAIL(status_write, position, "unable to write envelope");
    }

    out_traversal_result->src_out_of_line_size += src_contents_size;
    out_traversal_result->dst_out_of_line_size += dst_contents_size;
    out_traversal_result->handle_count += src_envelope->num_handles;

    return ZX_OK;
  }

  zx_status_t TransformXUnion(const FidlCodedXUnion& coded_xunion, const Position& position,
                              TraversalResult* out_traversal_result) {
    auto xunion = src_dst->Read<const fidl_xunion_t>(position);
    if (!xunion) {
      return TRANSFORMER_FAIL(ZX_ERR_BAD_STATE, position, "xunion missing");
    }

    const auto status_copy_xunion_hdr = src_dst->Copy(position, sizeof(fidl_xunion_t));
    if (status_copy_xunion_hdr != ZX_OK) {
      return status_copy_xunion_hdr;
    }

    const FidlXUnionField* field = nullptr;
    for (uint32_t i = 0; i < coded_xunion.field_count; i++) {
      const FidlXUnionField* candidate_field = coded_xunion.fields + i;
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

  zx_status_t TransformTable(const FidlCodedTable& coded_table, const Position& position,
                             TraversalResult* out_traversal_result) {
    auto table = src_dst->Read<const fidl_table_t>(position);
    if (!table) {
      return TRANSFORMER_FAIL(ZX_ERR_BAD_STATE, position, "table header missing");
    }

    const auto status_copy_table_hdr = src_dst->Copy(position, sizeof(fidl_table_t));
    if (status_copy_table_hdr != ZX_OK) {
      return TRANSFORMER_FAIL(status_copy_table_hdr, position, "unable to copy table header");
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
    const auto max_field_index = coded_table.field_count - 1;
    for (uint32_t ordinal = 1, field_index = 0; ordinal <= table->envelopes.count; ordinal++) {
      const FidlTableField& field = coded_table.fields[field_index];

      const bool known_field = (ordinal == field.ordinal);
      if (known_field) {
        if (field_index < max_field_index)
          field_index++;
      }

      TraversalResult envelope_traversal_result;
      zx_status_t status = TransformEnvelope(known_field, known_field ? field.type : nullptr,
                                             current_envelope_position, &envelope_traversal_result);
      if (status != ZX_OK) {
        return status;
      }

      current_envelope_position.src_inline_offset += static_cast<uint32_t>(sizeof(fidl_envelope_t));
      current_envelope_position.dst_inline_offset += static_cast<uint32_t>(sizeof(fidl_envelope_t));
      current_envelope_position.src_out_of_line_offset +=
          envelope_traversal_result.src_out_of_line_size;
      current_envelope_position.dst_out_of_line_offset +=
          envelope_traversal_result.dst_out_of_line_size;

      *out_traversal_result += envelope_traversal_result;
    }

    return ZX_OK;
  }

  zx_status_t TransformArray(const FidlCodedArrayNew& src_coded_array,
                             const FidlCodedArrayNew& dst_coded_array, const Position& position,
                             uint32_t dst_array_size, TraversalResult* out_traversal_result) {
    TRANSFORMER_ASSERT(src_coded_array.element_count == dst_coded_array.element_count, position);

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
        return TRANSFORMER_FAIL(status_pad, padding_position, "unable to pad array element");
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
      return TRANSFORMER_FAIL(status_pad, current_element_position, "unable to pad end of array");
    }

    return ZX_OK;
  }

  zx_status_t TransformUnionPointerToOptionalXUnion(const FidlCodedUnion& src_coded_union,
                                                    const FidlCodedXUnion& dst_coded_xunion,
                                                    const Position& position,
                                                    TraversalResult* out_traversal_result) {
    auto presence = src_dst->Read<uint64_t>(position);
    if (!presence) {
      return TRANSFORMER_FAIL(ZX_ERR_BAD_STATE, position, "union pointer missing");
    }

    switch (*presence) {
      case FIDL_ALLOC_ABSENT: {
        fidl_xunion_t absent = {};
        const auto status = src_dst->Write(position, absent);
        if (status != ZX_OK) {
          return TRANSFORMER_FAIL(status, position, "unable to write union pointer absense");
        }
        return ZX_OK;
      }
      case FIDL_ALLOC_PRESENT:
        // Ok
        break;
      default:
        return TRANSFORMER_FAIL(ZX_ERR_BAD_STATE, position, "union pointer invalid");
    }

    uint32_t src_aligned_size = FIDL_ALIGN(src_coded_union.size);
    const auto union_position = Position{
        position.src_out_of_line_offset,
        position.src_out_of_line_offset + src_aligned_size,
        position.dst_inline_offset,
        position.dst_out_of_line_offset,
    };

    out_traversal_result->src_out_of_line_size += src_aligned_size;
    return TransformUnionToXUnion(src_coded_union, dst_coded_xunion, union_position,
                                  0 /* unused: xunions are FIDL_ALIGNed */, out_traversal_result);
  }

  zx_status_t TransformUnionToXUnion(const FidlCodedUnion& src_coded_union,
                                     const FidlCodedXUnion& dst_coded_xunion,
                                     const Position& position, uint32_t /* unused: dst_size */,
                                     TraversalResult* out_traversal_result) {
    TRANSFORMER_ASSERT(src_coded_union.field_count == dst_coded_xunion.field_count, position);

    // Read: union tag.
    const auto union_tag_ptr =
        src_dst->Read<const fidl_union_tag_t>(position, src_coded_union.size);
    if (!union_tag_ptr) {
      return TRANSFORMER_FAIL(ZX_ERR_BAD_STATE, position, "union tag missing");
    }
    const auto union_tag = *union_tag_ptr;

    // Retrieve: union field/variant.
    if (union_tag >= src_coded_union.field_count) {
      return TRANSFORMER_FAIL(ZX_ERR_BAD_STATE, position, "invalid union tag");
    }

    const FidlUnionField& src_field = src_coded_union.fields[union_tag];
    const FidlXUnionField& dst_field = dst_coded_xunion.fields[union_tag];

    // Write: xunion tag & envelope.
    const uint32_t dst_inline_field_size = [&] {
      if (src_field.type) {
        return AltInlineSize(src_field.type, position);
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
      return TRANSFORMER_FAIL(status_pad_field, field_position,
                              "unable to pad union-as-xunion variant");
    }

    // Write envelope header.
    fidl_xunion_t xunion;
    xunion.tag = dst_field.ordinal;
    xunion.envelope.num_bytes = FIDL_ALIGN(dst_field_size);
    xunion.envelope.num_handles = field_traversal_result.handle_count;
    xunion.envelope.presence = FIDL_ALLOC_PRESENT;
    const auto status_write_xunion = src_dst->Write(position, xunion);
    if (status_write_xunion != ZX_OK) {
      return TRANSFORMER_FAIL(status_write_xunion, position,
                              "unable to write union-as-xunion header");
    }

    out_traversal_result->src_out_of_line_size += field_traversal_result.src_out_of_line_size;
    out_traversal_result->dst_out_of_line_size += FIDL_ALIGN(dst_field_size);
    out_traversal_result->handle_count += field_traversal_result.handle_count;

    return ZX_OK;
  }

  zx_status_t TransformOptionalXUnionToUnionPointer(const FidlCodedXUnion& src_coded_xunion,
                                                    const FidlCodedUnion& dst_coded_union,
                                                    const Position& position,
                                                    TraversalResult* out_traversal_result) {
    auto src_xunion = src_dst->Read<const fidl_xunion_t>(position);
    if (!src_xunion) {
      return TRANSFORMER_FAIL(ZX_ERR_BAD_STATE, position, "union-as-xunion missing");
    }

    switch (src_xunion->envelope.presence) {
      case FIDL_ALLOC_ABSENT:
      case FIDL_ALLOC_PRESENT: {
        const auto status = src_dst->Write(position, src_xunion->envelope.presence);
        if (status != ZX_OK) {
          return TRANSFORMER_FAIL(status, position, "unable to write union pointer absence");
        }
        if (src_xunion->envelope.presence == FIDL_ALLOC_ABSENT) {
          return ZX_OK;
        }
        break;
      }
      default:
        return TRANSFORMER_FAIL(ZX_ERR_BAD_STATE, position,
                                "union-as-xunion envelope presence invalid");
    }

    const uint32_t dst_aligned_size = FIDL_ALIGN(dst_coded_union.size);
    const auto union_position = Position{
        position.src_inline_offset,
        position.src_out_of_line_offset,
        position.dst_out_of_line_offset,
        position.dst_out_of_line_offset + dst_aligned_size,
    };

    out_traversal_result->dst_out_of_line_size += dst_aligned_size;

    return TransformXUnionToUnion(src_coded_xunion, dst_coded_union, union_position,
                                  FIDL_ALIGN(dst_coded_union.size), out_traversal_result);
  }

  zx_status_t TransformXUnionToUnion(const FidlCodedXUnion& src_coded_xunion,
                                     const FidlCodedUnion& dst_coded_union,
                                     const Position& position, uint32_t dst_size,
                                     TraversalResult* out_traversal_result) {
    TRANSFORMER_ASSERT(src_coded_xunion.field_count == dst_coded_union.field_count, position);

    // Read: extensible-union ordinal.
    const auto src_xunion = src_dst->Read<const fidl_xunion_t>(position);
    if (!src_xunion) {
      return TRANSFORMER_FAIL(ZX_ERR_BAD_STATE, position, "union-as-xunion missing");
    }

    switch (src_xunion->envelope.presence) {
      case FIDL_ALLOC_PRESENT:
        // OK
        break;
      case FIDL_ALLOC_ABSENT:
        return TRANSFORMER_FAIL(ZX_ERR_BAD_STATE, position,
                                "union-as-xunion envelope is invalid FIDL_ALLOC_ABSENT");
      default:
        return TRANSFORMER_FAIL(ZX_ERR_BAD_STATE, position,
                                "union-as-xunion envelope presence invalid");
    }

    // Retrieve: flexible-union field (or variant).
    bool src_field_found = false;
    uint32_t src_field_index = 0;
    const FidlXUnionField* src_field = nullptr;
    for (/* src_field_index needed after the loop */;
         src_field_index < src_coded_xunion.field_count; src_field_index++) {
      const FidlXUnionField* candidate_src_field = &src_coded_xunion.fields[src_field_index];
      if (candidate_src_field->ordinal == src_xunion->tag) {
        src_field_found = true;
        src_field = candidate_src_field;
        break;
      }
    }
    if (!src_field_found) {
      return TRANSFORMER_FAIL(ZX_ERR_BAD_STATE, position, "ordinal has no corresponding variant");
    }

    const FidlUnionField& dst_field = dst_coded_union.fields[src_field_index];

    // Write: static-union tag, and pad (if needed).
    switch (dst_coded_union.data_offset) {
      case 4: {
        const auto status = src_dst->Write(position, src_field_index);
        if (status != ZX_OK) {
          return TRANSFORMER_FAIL(status, position, "unable to write union tag");
        }
        break;
      }
      case 8: {
        const auto status = src_dst->Write(position, static_cast<uint64_t>(src_field_index));
        if (status != ZX_OK) {
          return TRANSFORMER_FAIL(status, position, "unable to write union tag");
        }
        break;
      }
      default:
        TRANSFORMER_ASSERT(false && "static-union data offset can only be 4 or 8", position);
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

      // TODO(apang): Add test to verify that we _should_ FIDL_ALIGN below.
      return FIDL_ALIGN(InlineSize(src_field->type, From(), position));
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
      return TRANSFORMER_FAIL(status_pad_field, field_padding_position,
                              "unable to pad union variant");
    }

    out_traversal_result->src_out_of_line_size += src_field_inline_size;
    return ZX_OK;
  }

  virtual WireFormat From() const = 0;
  virtual WireFormat To() const = 0;

  inline zx_status_t Fail(zx_status_t status, const Position& position, const int line_number,
                          const char* error_msg) {
    debug_info_->RecordFailure(line_number, error_msg, position);
    return status;
  }

  SrcDst* src_dst;
  DebugInfo* const debug_info_;

 private:
  const fidl_type_t* top_level_type_;
};

// TODO(apang): Mark everything override
// TODO(apang): We can remove the V1ToOld & OldToV1 classes since they only have From() and To()
// methods now, which can be passed into the constructor of TransformerBase.
class V1ToOld final : public TransformerBase {
 public:
  V1ToOld(SrcDst* src_dst, const fidl_type_t* top_level_type, DebugInfo* debug_info)
      : TransformerBase(src_dst, top_level_type, debug_info) {}

  WireFormat From() const { return WireFormat::kV1; }
  WireFormat To() const { return WireFormat::kOld; }
};

class OldToV1 final : public TransformerBase {
 public:
  OldToV1(SrcDst* src_dst, const fidl_type_t* top_level_type, DebugInfo* debug_info)
      : TransformerBase(src_dst, top_level_type, debug_info) {}

 private:
  // TODO(apang): Could CRTP this.
  WireFormat From() const { return WireFormat::kOld; }
  WireFormat To() const { return WireFormat::kV1; }
};

}  // namespace

zx_status_t fidl_transform(fidl_transformation_t transformation, const fidl_type_t* type,
                           const uint8_t* src_bytes, uint32_t src_num_bytes, uint8_t* dst_bytes,
                           uint32_t dst_num_bytes_capacity, uint32_t* out_dst_num_bytes,
                           const char** out_error_msg) {
  if (!type || !src_bytes || !dst_bytes || !out_dst_num_bytes || !FidlIsAligned(src_bytes) ||
      !FidlIsAligned(dst_bytes)) {
    return ZX_ERR_INVALID_ARGS;
  }

  SrcDst src_dst(src_bytes, src_num_bytes, dst_bytes, dst_num_bytes_capacity);
  DebugInfo debug_info(transformation, type, src_dst, out_error_msg);

  const zx_status_t status = [&] {
    switch (transformation) {
      case FIDL_TRANSFORMATION_NONE: {
        const auto start = Position{
            0, UINT16_MAX, /* unused: src_out_of_line_offset */
            0, UINT16_MAX, /* unused: dst_out_of_line_offset */
        };
        return src_dst.Copy(start, src_num_bytes);
      }
      case FIDL_TRANSFORMATION_V1_TO_OLD:
        return V1ToOld(&src_dst, type, &debug_info).TransformTopLevelStruct();
      case FIDL_TRANSFORMATION_OLD_TO_V1:
        return OldToV1(&src_dst, type, &debug_info).TransformTopLevelStruct();
      default:
        debug_info.RecordFailure(__LINE__, "unsupported transformation");
        return ZX_ERR_INVALID_ARGS;
    }
  }();

  if (status != ZX_OK) {
    return status;
  }

  if (FIDL_ALIGN(src_dst.src_max_offset_read()) != src_num_bytes) {
    debug_info.RecordFailure(__LINE__, "did not read all provided bytes during transformation");
    return ZX_ERR_INVALID_ARGS;
  }

  *out_dst_num_bytes = src_dst.dst_max_offset_written();
  return ZX_OK;
}

#pragma GCC diagnostic pop  // "-Wimplicit-fallthrough"
