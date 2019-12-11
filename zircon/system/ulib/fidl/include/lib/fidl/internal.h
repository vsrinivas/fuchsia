// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_INTERNAL_H_
#define LIB_FIDL_INTERNAL_H_

#include <assert.h>
#include <lib/fidl/coding.h>
#include <stdbool.h>
#include <stdint.h>
#include <zircon/syscalls/object.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// All sizes here are given as uint32_t. Fidl message sizes are bounded to well below UINT32_MAX.
// This also applies to arrays and vectors. For arrays, element_count * element_size will always fit
// with 32 bits. For vectors, max_count * element_size will always fit within 32 bits.

// Pointers to other type tables within a type are always nonnull, with the exception of vectors.
// In that case, a null pointer indicates that the element type of the vector has no interesting
// information to be decoded (i.e. no pointers or handles). The vector type still needs to be
// emitted as it contains the information about the size of its secondary object. Contrast this with
// arrays: being inline, ones with no interesting coding information can be elided, just like a
// uint32 field in a struct is elided.

typedef bool FidlNullability;
static const bool kFidlNullability_Nonnullable = false;
static const bool kFidlNullability_Nullable = true;

typedef bool FidlStrictness;
static const bool kFidlStrictness_Flexible = false;
static const bool kFidlStrictness_Strict = true;

// TODO(fxb/42792): Remove either this FidlAlign function or the FIDL_ALIGN macro in zircon/fidl.h.
#ifdef __cplusplus
constexpr
#endif
static inline uint64_t FidlAlign(uint32_t offset) {
  const uint64_t alignment_mask = FIDL_ALIGNMENT - 1;
  return (offset + alignment_mask) & ~alignment_mask;
}

// Determine if the pointer is aligned to |FIDL_ALIGNMENT|.
static inline bool FidlIsAligned(const uint8_t* ptr) {
  uintptr_t uintptr = (uintptr_t)(ptr);
  const uintptr_t kAlignment = FIDL_ALIGNMENT;
  return uintptr % kAlignment == 0;
}

// Add |size| to out-of-line |offset|, maintaining alignment. For example, a pointer to a struct
// that is 4 bytes still needs to advance the next out-of-line offset by 8 to maintain
// the aligned-to-FIDL_ALIGNMENT property.
// Returns false on overflow. Otherwise, resulting offset is stored in |out_offset|.
static inline bool FidlAddOutOfLine(uint32_t offset, uint32_t size, uint32_t* out_offset) {
  const uint32_t kMask = FIDL_ALIGNMENT - 1;
  uint32_t new_offset = offset;
  if (add_overflow(new_offset, size, &new_offset) || add_overflow(new_offset, kMask, &new_offset)) {
    return false;
  }
  new_offset &= ~kMask;
  *out_offset = new_offset;
  return true;
}

struct FidlStructField {
  const fidl_type_t* type;

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
  const struct FidlStructField* alt_field;

#ifdef __cplusplus
  // TODO(fxb/39680): Update this constructor, so that a variant is available where not supplying
  // the |alt_field| parameter forces the constructor caller to acknowledge it's for the old wire
  // format only.
  constexpr FidlStructField(const fidl_type* type, uint32_t offset, uint8_t padding,
                            const FidlStructField* alt_field = nullptr)
      : type(type), offset(offset), padding(padding), alt_field(alt_field) {}
#endif  // __cplusplus
};

struct FidlUnionField {
  const fidl_type_t* type;
  uint32_t padding;
  uint32_t xunion_ordinal;
};

struct FidlTableField {
  const fidl_type_t* type;
  uint32_t ordinal;
};

struct FidlXUnionField {
  const fidl_type_t* type;
  uint32_t ordinal;
};

// TODO(fxb/42793): Consider starting enum values for FidlTypeTag from 1, not 0.
typedef uint32_t FidlTypeTag;
static const uint32_t kFidlTypePrimitive = 0;
static const uint32_t kFidlTypeEnum = 1;
static const uint32_t kFidlTypeBits = 2;
static const uint32_t kFidlTypeStruct = 3;
static const uint32_t kFidlTypeStructPointer = 4;
static const uint32_t kFidlTypeUnion = 5;
static const uint32_t kFidlTypeUnionPointer = 6;
static const uint32_t kFidlTypeArray = 7;
static const uint32_t kFidlTypeString = 8;
static const uint32_t kFidlTypeHandle = 9;
static const uint32_t kFidlTypeVector = 10;
static const uint32_t kFidlTypeTable = 11;
static const uint32_t kFidlTypeXUnion = 12;

// TODO(fxb/42793): Consider starting enum values for FidlCodedPrimitive from 1, not 0.
typedef uint32_t FidlCodedPrimitive;
static const uint32_t kFidlCodedPrimitive_Bool = 0;
static const uint32_t kFidlCodedPrimitive_Int8 = 1;
static const uint32_t kFidlCodedPrimitive_Int16 = 2;
static const uint32_t kFidlCodedPrimitive_Int32 = 3;
static const uint32_t kFidlCodedPrimitive_Int64 = 4;
static const uint32_t kFidlCodedPrimitive_Uint8 = 5;
static const uint32_t kFidlCodedPrimitive_Uint16 = 6;
static const uint32_t kFidlCodedPrimitive_Uint32 = 7;
static const uint32_t kFidlCodedPrimitive_Uint64 = 8;
static const uint32_t kFidlCodedPrimitive_Float32 = 9;
static const uint32_t kFidlCodedPrimitive_Float64 = 10;

typedef bool (*EnumValidationPredicate)(uint64_t);

struct FidlCodedEnum {
  const FidlCodedPrimitive underlying_type;
  const EnumValidationPredicate validate;
  const char* name;  // may be nullptr if omitted at compile time
};

struct FidlCodedBits {
  const FidlCodedPrimitive underlying_type;
  const uint64_t mask;
  const char* name;  // may be nullptr if omitted at compile time
};

// Though the |size| is implied by the fields, computing that information is not
// the purview of this library. It's easier for the compiler to stash it.
struct FidlCodedStruct {
  const struct FidlStructField* const fields;
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
  const struct FidlCodedStruct* const alt_type;
};

struct FidlCodedStructPointer {
  const struct FidlCodedStruct* const struct_type;
};

struct FidlCodedTable {
  const struct FidlTableField* const fields;
  const uint32_t field_count;
  const char* name;  // may be nullptr if omitted at compile time
};

// On-the-wire unions begin with a tag which is an index into |fields|.
// |data_offset| is the offset of the data in the wire format (tag + padding).
struct FidlCodedUnion {
  const struct FidlUnionField* const fields;
  const uint32_t field_count;
  const uint32_t data_offset;
  const uint32_t size;
  const char* name;  // may be nullptr if omitted at compile time

  // Pointer to the alternate ("alt") version of this FidlCodedUnion, which is the v1 version of the
  // union if this is the old union; or the old version of the union if this is the v1 version.
  const struct FidlCodedUnion* const alt_type;
};

struct FidlCodedUnionPointer {
  const struct FidlCodedUnion* const union_type;
};

struct FidlCodedXUnion {
  const uint32_t field_count;
  const struct FidlXUnionField* const fields;
  const FidlNullability nullable;
  const char* name;  // may be nullptr if omitted at compile time
  const FidlStrictness strictness;
};

// An array is essentially a struct with |array_size / element_size| of the same field, named at
// |element|.
struct FidlCodedArray {
  const fidl_type_t* const element;
  const uint32_t array_size;
  const uint32_t element_size;

  // Pointer to the alternate ("alt") version of this FidlCodedArray, which is the v1 version of the
  // array if this is the old struct; or the old version of the array if this is the v1 version.
  const struct FidlCodedArray* const alt_type;
};

// TODO(fxb/39388): Switch to using this more ergonomic coding table for arrays.
struct FidlCodedArrayNew {
  const fidl_type_t* const element;
  const uint64_t element_count;
  const uint32_t element_size;
  const uint32_t element_padding;
  const struct FidlCodedArrayNew* const alt_type;
};

struct FidlCodedHandle {
  const zx_obj_type_t handle_subtype;
  const FidlNullability nullable;

  static_assert(ZX_OBJ_TYPE_UPPER_BOUND <= UINT32_MAX, "");
};

struct FidlCodedString {
  const uint32_t max_size;
  const FidlNullability nullable;
};

// Note that |max_count * element_size| is guaranteed to fit into a uint32_t. Unlike other types,
// the |element| pointer may be null. This occurs when the element type contains no interesting bits
// (i.e. pointers or handles).
struct FidlCodedVector {
  const fidl_type_t* const element;
  const uint32_t max_count;
  const uint32_t element_size;
  const FidlNullability nullable;

  // Pointer to the alternate ("alt") version of this FidlCodedVector, which is the v1 version of
  // the vector if this is the old struct; or the old version of the vector if this is the v1
  // version.
  const struct FidlCodedVector* const alt_type;
};

struct fidl_type {
  const FidlTypeTag type_tag;
  const union {
    const FidlCodedPrimitive coded_primitive;
    const struct FidlCodedEnum coded_enum;
    const struct FidlCodedBits coded_bits;
    const struct FidlCodedStruct coded_struct;
    const struct FidlCodedStructPointer coded_struct_pointer;
    const struct FidlCodedTable coded_table;
    const struct FidlCodedUnion coded_union;
    const struct FidlCodedUnionPointer coded_union_pointer;
    const struct FidlCodedXUnion coded_xunion;
    const struct FidlCodedHandle coded_handle;
    const struct FidlCodedString coded_string;
    const struct FidlCodedArray coded_array;
    const struct FidlCodedVector coded_vector;
  };
};

extern const fidl_type_t fidl_internal_kBoolTable;
extern const fidl_type_t fidl_internal_kInt8Table;
extern const fidl_type_t fidl_internal_kInt16Table;
extern const fidl_type_t fidl_internal_kInt32Table;
extern const fidl_type_t fidl_internal_kInt64Table;
extern const fidl_type_t fidl_internal_kUint8Table;
extern const fidl_type_t fidl_internal_kUint16Table;
extern const fidl_type_t fidl_internal_kUint32Table;
extern const fidl_type_t fidl_internal_kUint64Table;
extern const fidl_type_t fidl_internal_kFloat32Table;
extern const fidl_type_t fidl_internal_kFloat64Table;

__END_CDECLS

#endif  // LIB_FIDL_INTERNAL_H_
