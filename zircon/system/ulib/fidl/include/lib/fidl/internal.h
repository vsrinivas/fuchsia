// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_INTERNAL_H_
#define LIB_FIDL_INTERNAL_H_

#include <assert.h>
#include <lib/fidl/coding.h>
#include <zircon/syscalls/object.h>
#include <zircon/types.h>

#include <cstdint>

// All sizes here are given as uint32_t. Fidl message sizes are bounded to well below UINT32_MAX.
// This also applies to arrays and vectors. For arrays, element_count * element_size will always fit
// with 32 bits. For vectors, max_count * element_size will always fit within 32 bits.

// Pointers to other type tables within a type are always nonnull, with the exception of vectors.
// In that case, a null pointer indicates that the element type of the vector has no interesting
// information to be decoded (i.e. no pointers or handles). The vector type still needs to be
// emitted as it contains the information about the size of its secondary object. Contrast this with
// arrays: being inline, ones with no interesting coding information can be elided, just like a
// uint32 field in a struct is elided.

namespace fidl {

enum FidlNullability : bool {
  kNonnullable = false,
  kNullable = true,
};

enum FidlStrictness : bool {
  kFlexible = false,
  kStrict = true,
};

constexpr inline uint64_t FidlAlign(uint32_t offset) {
  constexpr uint64_t alignment_mask = FIDL_ALIGNMENT - 1;
  return (offset + alignment_mask) & ~alignment_mask;
}

// Determine if the pointer is aligned to |FIDL_ALIGNMENT|.
inline bool IsAligned(const uint8_t* ptr) {
  auto uintptr = reinterpret_cast<std::uintptr_t>(ptr);
  constexpr uintptr_t kAlignment = FIDL_ALIGNMENT;
  return uintptr % kAlignment == 0;
}

// Add |size| to out-of-line |offset|, maintaining alignment. For example, a pointer to a struct
// that is 4 bytes still needs to advance the next out-of-line offset by 8 to maintain
// the aligned-to-FIDL_ALIGNMENT property.
// Returns false on overflow. Otherwise, resulting offset is stored in |out_offset|.
inline bool AddOutOfLine(uint32_t offset, uint32_t size, uint32_t* out_offset) {
  constexpr uint32_t kMask = FIDL_ALIGNMENT - 1;
  uint32_t new_offset = offset;
  if (add_overflow(new_offset, size, &new_offset) || add_overflow(new_offset, kMask, &new_offset)) {
    return false;
  }
  new_offset &= ~kMask;
  *out_offset = new_offset;
  return true;
}

struct FidlStructField {
  const fidl_type* type;

  // If |type| is not nullptr, |offset| stores the offset of the struct member.
  // If |type| is nullptr, |padding_offset| stores the offset where padding starts.
  union {
    uint32_t offset;
    uint32_t padding_offset;
  };
  uint8_t padding;

  // Pointer to the alternate ("alt") version of this FidlStructField, which is
  // the v1 version of the struct field if this is the old struct field; or the
  // old version of the struct field if this is the v1 version.
  const FidlStructField* alt_field;

  // TODO(fxb/39680): Update this constructor, so that a variant is available where not supplying
  // the |alt_field| parameter forces the constructor caller to acknowledge it's for the old wire
  // format only.
  constexpr FidlStructField(const fidl_type* type, uint32_t offset, uint8_t padding,
                            const FidlStructField* alt_field = nullptr)
      : type(type), offset(offset), padding(padding), alt_field(alt_field) {}
};

struct FidlUnionField {
  const fidl_type* type;
  uint32_t padding;
  uint32_t xunion_ordinal;

  // TODO(fxb/39680): Update this constructor, so that a variant is available where not supplying
  // the |xunion_ordinal| parameter forces the constructor caller to acknowledge it's for the old
  // wire format only.
  constexpr FidlUnionField(const fidl_type* type, uint32_t padding, uint32_t xunion_ordinal = 0)
      : type(type), padding(padding), xunion_ordinal(xunion_ordinal) {}
};

struct FidlTableField {
  const fidl_type* type;
  uint32_t ordinal;

  constexpr FidlTableField(const fidl_type* type, uint32_t ordinal)
      : type(type), ordinal(ordinal) {}
};

struct FidlXUnionField {
  const fidl_type* type;
  uint32_t ordinal;

  constexpr FidlXUnionField(const fidl_type* type, uint32_t ordinal)
      : type(type), ordinal(ordinal) {}
};

enum FidlTypeTag : uint32_t {
  kFidlTypePrimitive,
  kFidlTypeEnum,
  kFidlTypeBits,
  kFidlTypeStruct,
  kFidlTypeStructPointer,
  kFidlTypeUnion,
  kFidlTypeUnionPointer,
  kFidlTypeArray,
  kFidlTypeString,
  kFidlTypeHandle,
  kFidlTypeVector,
  kFidlTypeTable,
  kFidlTypeXUnion,
};

enum struct FidlCodedPrimitive : uint32_t {
  kBool,
  kInt8,
  kInt16,
  kInt32,
  kInt64,
  kUint8,
  kUint16,
  kUint32,
  kUint64,
  kFloat32,
  kFloat64,
};

typedef bool (*EnumValidationPredicate)(uint64_t);

struct FidlCodedEnum {
  const FidlCodedPrimitive underlying_type;
  const EnumValidationPredicate validate;
  const char* name;  // may be nullptr if omitted at compile time

  constexpr explicit FidlCodedEnum(FidlCodedPrimitive underlying_type,
                                   EnumValidationPredicate validate, const char* name)
      : underlying_type(underlying_type), validate(validate), name(name) {}
};

struct FidlCodedBits {
  const FidlCodedPrimitive underlying_type;
  const uint64_t mask;
  const char* name;  // may be nullptr if omitted at compile time

  constexpr explicit FidlCodedBits(FidlCodedPrimitive underlying_type, uint64_t mask,
                                   const char* name)
      : underlying_type(underlying_type), mask(mask), name(name) {}
};

// Though the |size| is implied by the fields, computing that information is not the purview of this
// library. It's easier for the compiler to stash it.
struct FidlCodedStruct {
  const FidlStructField* const fields;
  const uint32_t field_count;
  const uint32_t size;
  // The max_out_of_line and contains_union fields are only used by the HLCPP bindings for
  // optimizations when validating v1 bytes of a transactional message before sending.
  const uint32_t max_out_of_line;
  const bool contains_union;
  const char* name;  // may be nullptr if omitted at compile time

  // Pointer to the alternate ("alt") version of this FidlCodedStruct, which is the v1 version of
  // the struct if this is the old struct; or the old version of the struct if this is the v1
  // version.
  const FidlCodedStruct* const alt_type;

  // TODO(fxb/39680): Update this constructor, so that a variant is available where not supplying
  // the |alt_type| parameter forces the constructor caller to acknowledge it's for the old wire
  // format only.
  constexpr FidlCodedStruct(const FidlStructField* fields, uint32_t field_count, uint32_t size,
                            uint32_t max_out_of_line, bool contains_union, const char* name,
                            const FidlCodedStruct* const alt_type = nullptr)
      : fields(fields),
        field_count(field_count),
        size(size),
        max_out_of_line(max_out_of_line),
        contains_union(contains_union),
        name(name),
        alt_type(alt_type) {}

  // Since the max_out_of_line and contains_union fields are only used by the HLCPP bindings during
  // the wire format migration, this constructor is added to avoid having to temporarily update
  // hand written coding tables that are only used for testing non HLCPP bindings.
  constexpr FidlCodedStruct(const FidlStructField* fields, uint32_t field_count, uint32_t size,
                            const char* name, const FidlCodedStruct* const alt_type = nullptr)
      : fields(fields),
        field_count(field_count),
        size(size),
        max_out_of_line(UINT32_MAX),
        contains_union(true),
        name(name),
        alt_type(alt_type) {}
};

struct FidlCodedStructPointer {
  const FidlCodedStruct* const struct_type;

  constexpr explicit FidlCodedStructPointer(const FidlCodedStruct* struct_type)
      : struct_type(struct_type) {}
};

struct FidlCodedTable {
  const FidlTableField* const fields;
  const uint32_t field_count;
  const char* name;  // may be nullptr if omitted at compile time

  constexpr FidlCodedTable(const FidlTableField* fields, uint32_t field_count, const char* name)
      : fields(fields), field_count(field_count), name(name) {}
};

// On-the-wire unions begin with a tag which is an index into |fields|.
// |data_offset| is the offset of the data in the wire format (tag + padding).
struct FidlCodedUnion {
  const FidlUnionField* const fields;
  const uint32_t field_count;
  const uint32_t data_offset;
  const uint32_t size;
  const char* name;  // may be nullptr if omitted at compile time

  // Pointer to the alternate ("alt") version of this FidlCodedUnion, which is the v1 version of the
  // union if this is the old union; or the old version of the union if this is the v1 version.
  const FidlCodedUnion* const alt_type;

  // TODO(fxb/39680): Update this constructor, so that a variant is available where not supplying
  // the |alt_type| parameter forces the constructor caller to acknowledge it's for the old wire
  // format only.
  constexpr FidlCodedUnion(const FidlUnionField* const fields, uint32_t field_count,
                           uint32_t data_offset, uint32_t size, const char* name,
                           const FidlCodedUnion* const alt_type = nullptr)
      : fields(fields),
        field_count(field_count),
        data_offset(data_offset),
        size(size),
        name(name),
        alt_type(alt_type) {}
};

struct FidlCodedUnionPointer {
  const FidlCodedUnion* const union_type;

  constexpr explicit FidlCodedUnionPointer(const FidlCodedUnion* union_type)
      : union_type(union_type) {}
};

struct FidlCodedXUnion {
  const uint32_t field_count;
  const FidlXUnionField* const fields;
  const FidlNullability nullable;
  const char* name;  // may be nullptr if omitted at compile time
  const FidlStrictness strictness;

  constexpr FidlCodedXUnion(uint32_t field_count, const FidlXUnionField* fields,
                            FidlNullability nullable, const char* name, FidlStrictness strictness)
      : field_count(field_count),
        fields(fields),
        nullable(nullable),
        name(name),
        strictness(strictness) {}
};

// An array is essentially a struct with |array_size / element_size| of the same field, named at
// |element|.
struct FidlCodedArray {
  const fidl_type* const element;
  const uint32_t array_size;
  const uint32_t element_size;

  // Pointer to the alternate ("alt") version of this FidlCodedArray, which is the v1 version of the
  // array if this is the old struct; or the old version of the array if this is the v1 version.
  const FidlCodedArray* const alt_type;

  // TODO(fxb/39680): Update this constructor, so that a variant is available where not supplying
  // the |alt_type| parameter forces the constructor caller to acknowledge it's for the old wire
  // format only.
  constexpr FidlCodedArray(const fidl_type* element, uint32_t array_size, uint32_t element_size,
                           const FidlCodedArray* const alt_type = nullptr)
      : element(element), array_size(array_size), element_size(element_size), alt_type(alt_type) {}
};

// TODO(fxb/39388): Switch to using this more ergonomic coding table for arrays.
struct FidlCodedArrayNew {
  const fidl_type* const element;
  const uint64_t element_count;
  const uint32_t element_size;
  const uint32_t element_padding;
  const FidlCodedArrayNew* alt_type;

  constexpr FidlCodedArrayNew(const fidl_type* element, uint64_t element_count,
                              uint32_t element_size, uint32_t element_padding,
                              const FidlCodedArrayNew* alt_type)
      : element(element),
        element_count(element_count),
        element_size(element_size),
        element_padding(element_padding),
        alt_type(alt_type) {}
};

// Note: must keep in sync with fidlc types.h HandleSubtype.
enum FidlHandleSubtype : zx_obj_type_t {
  // special case to indicate subtype is not specified.
  kFidlHandleSubtypeHandle = ZX_OBJ_TYPE_NONE,

  kFidlHandleSubtypeBti = ZX_OBJ_TYPE_BTI,
  kFidlHandleSubtypeChannel = ZX_OBJ_TYPE_CHANNEL,
  kFidlHandleSubtypeEvent = ZX_OBJ_TYPE_EVENT,
  kFidlHandleSubtypeEventpair = ZX_OBJ_TYPE_EVENTPAIR,
  kFidlHandleSubtypeException = ZX_OBJ_TYPE_EXCEPTION,
  kFidlHandleSubtypeFifo = ZX_OBJ_TYPE_FIFO,
  kFidlHandleSubtypeGuest = ZX_OBJ_TYPE_GUEST,
  kFidlHandleSubtypeInterrupt = ZX_OBJ_TYPE_INTERRUPT,
  kFidlHandleSubtypeIommu = ZX_OBJ_TYPE_IOMMU,
  kFidlHandleSubtypeJob = ZX_OBJ_TYPE_JOB,
  kFidlHandleSubtypeLog = ZX_OBJ_TYPE_LOG,
  kFidlHandleSubtypePager = ZX_OBJ_TYPE_PAGER,
  kFidlHandleSubtypePciDevice = ZX_OBJ_TYPE_PCI_DEVICE,
  kFidlHandleSubtypePmt = ZX_OBJ_TYPE_PMT,
  kFidlHandleSubtypePort = ZX_OBJ_TYPE_PORT,
  kFidlHandleSubtypeProcess = ZX_OBJ_TYPE_PROCESS,
  kFidlHandleSubtypeProfile = ZX_OBJ_TYPE_PROFILE,
  kFidlHandleSubtypeResource = ZX_OBJ_TYPE_RESOURCE,
  kFidlHandleSubtypeSocket = ZX_OBJ_TYPE_SOCKET,
  kFidlHandleSubtypeSuspendToken = ZX_OBJ_TYPE_SUSPEND_TOKEN,
  kFidlHandleSubtypeThread = ZX_OBJ_TYPE_THREAD,
  kFidlHandleSubtypeTimer = ZX_OBJ_TYPE_TIMER,
  kFidlHandleSubtypeVcpu = ZX_OBJ_TYPE_VCPU,
  kFidlHandleSubtypeVmar = ZX_OBJ_TYPE_VMAR,
  kFidlHandleSubtypeVmo = ZX_OBJ_TYPE_VMO,
};

struct FidlCodedHandle {
  const zx_obj_type_t handle_subtype;
  const FidlNullability nullable;

  constexpr FidlCodedHandle(uint32_t handle_subtype, FidlNullability nullable)
      : handle_subtype(handle_subtype), nullable(nullable) {}

  static_assert(ZX_OBJ_TYPE_UPPER_BOUND <= UINT32_MAX, "");
};

struct FidlCodedString {
  const uint32_t max_size;
  const FidlNullability nullable;

  constexpr FidlCodedString(uint32_t max_size, FidlNullability nullable)
      : max_size(max_size), nullable(nullable) {}
};

// Note that |max_count * element_size| is guaranteed to fit into a uint32_t. Unlike other types,
// the |element| pointer may be null. This occurs when the element type contains no interesting bits
// (i.e. pointers or handles).
struct FidlCodedVector {
  const fidl_type* const element;
  const uint32_t max_count;
  const uint32_t element_size;
  const FidlNullability nullable;

  // Pointer to the alternate ("alt") version of this FidlCodedVector, which is the v1 version of
  // the vector if this is the old struct; or the old version of the vector if this is the v1
  // version.
  const FidlCodedVector* const alt_type;

  // TODO(fxb/39680): Update this constructor, so that a variant is available where not supplying
  // the |alt_type| parameter forces the constructor caller to acknowledge it's for the old wire
  // format only.
  constexpr FidlCodedVector(const fidl_type* element, uint32_t max_count, uint32_t element_size,
                            FidlNullability nullable,
                            const FidlCodedVector* const alt_type = nullptr)
      : element(element),
        max_count(max_count),
        element_size(element_size),
        nullable(nullable),
        alt_type(alt_type) {}
};

}  // namespace fidl

struct fidl_type {
  const fidl::FidlTypeTag type_tag;
  const union {
    const fidl::FidlCodedPrimitive coded_primitive;
    const fidl::FidlCodedEnum coded_enum;
    const fidl::FidlCodedBits coded_bits;
    const fidl::FidlCodedStruct coded_struct;
    const fidl::FidlCodedStructPointer coded_struct_pointer;
    const fidl::FidlCodedTable coded_table;
    const fidl::FidlCodedUnion coded_union;
    const fidl::FidlCodedUnionPointer coded_union_pointer;
    const fidl::FidlCodedXUnion coded_xunion;
    const fidl::FidlCodedHandle coded_handle;
    const fidl::FidlCodedString coded_string;
    const fidl::FidlCodedArray coded_array;
    const fidl::FidlCodedVector coded_vector;
  };

  constexpr fidl_type(fidl::FidlCodedPrimitive coded_primitive) noexcept
      : type_tag(fidl::kFidlTypePrimitive), coded_primitive(coded_primitive) {}

  constexpr fidl_type(fidl::FidlCodedEnum coded_enum) noexcept
      : type_tag(fidl::kFidlTypeEnum), coded_enum(coded_enum) {}

  constexpr fidl_type(fidl::FidlCodedBits coded_bits) noexcept
      : type_tag(fidl::kFidlTypeBits), coded_bits(coded_bits) {}

  constexpr fidl_type(fidl::FidlCodedStruct coded_struct) noexcept
      : type_tag(fidl::kFidlTypeStruct), coded_struct(coded_struct) {}

  constexpr fidl_type(fidl::FidlCodedStructPointer coded_struct_pointer) noexcept
      : type_tag(fidl::kFidlTypeStructPointer), coded_struct_pointer(coded_struct_pointer) {}

  constexpr fidl_type(fidl::FidlCodedTable coded_table) noexcept
      : type_tag(fidl::kFidlTypeTable), coded_table(coded_table) {}

  constexpr fidl_type(fidl::FidlCodedUnion coded_union) noexcept
      : type_tag(fidl::kFidlTypeUnion), coded_union(coded_union) {}

  constexpr fidl_type(fidl::FidlCodedUnionPointer coded_union_pointer) noexcept
      : type_tag(fidl::kFidlTypeUnionPointer), coded_union_pointer(coded_union_pointer) {}

  constexpr fidl_type(fidl::FidlCodedXUnion coded_xunion) noexcept
      : type_tag(fidl::kFidlTypeXUnion), coded_xunion(coded_xunion) {}

  constexpr fidl_type(fidl::FidlCodedHandle coded_handle) noexcept
      : type_tag(fidl::kFidlTypeHandle), coded_handle(coded_handle) {}

  constexpr fidl_type(fidl::FidlCodedString coded_string) noexcept
      : type_tag(fidl::kFidlTypeString), coded_string(coded_string) {}

  constexpr fidl_type(fidl::FidlCodedArray coded_array) noexcept
      : type_tag(fidl::kFidlTypeArray), coded_array(coded_array) {}

  constexpr fidl_type(fidl::FidlCodedVector coded_vector) noexcept
      : type_tag(fidl::kFidlTypeVector), coded_vector(coded_vector) {}
};

namespace fidl {

namespace internal {

extern const fidl_type kBoolTable;
extern const fidl_type kInt8Table;
extern const fidl_type kInt16Table;
extern const fidl_type kInt32Table;
extern const fidl_type kInt64Table;
extern const fidl_type kUint8Table;
extern const fidl_type kUint16Table;
extern const fidl_type kUint32Table;
extern const fidl_type kUint64Table;
extern const fidl_type kFloat32Table;
extern const fidl_type kFloat64Table;

}  // namespace internal

}  // namespace fidl

#endif  // LIB_FIDL_INTERNAL_H_
