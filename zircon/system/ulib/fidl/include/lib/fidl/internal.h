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

#ifdef __cplusplus
#include <type_traits>
#endif  // __cplusplus

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
// clang-format off
#ifdef __cplusplus
constexpr
#endif  // __cplusplus
static inline uint64_t FidlAlign(uint32_t offset) {
  const uint64_t alignment_mask = FIDL_ALIGNMENT - 1;
  return (offset + alignment_mask) & ~alignment_mask;
}
// clang-format on

// Determine if the pointer is aligned to |FIDL_ALIGNMENT|.
static inline bool FidlIsAligned(const uint8_t* ptr) {
  uintptr_t uintptr = (uintptr_t)(ptr);
  const uint64_t alignment_mask = FIDL_ALIGNMENT - 1;
  return (uintptr & alignment_mask) == 0;
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

typedef uint8_t FidlStructElementType;
static const FidlStructElementType kFidlStructElementType_Field = (uint8_t)1u;
static const FidlStructElementType kFidlStructElementType_Padding64 = (uint8_t)2u;
static const FidlStructElementType kFidlStructElementType_Padding32 = (uint8_t)3u;
static const FidlStructElementType kFidlStructElementType_Padding16 = (uint8_t)4u;

typedef bool FidlIsResource;
static const FidlIsResource kFidlIsResource_Resource = true;
static const FidlIsResource kFidlIsResource_NotResource = false;

struct FidlStructElementHeader {
  FidlStructElementType element_type;
  FidlIsResource is_resource;
};

struct FidlStructField {
  struct FidlStructElementHeader header;

  uint32_t offset;
  const fidl_type_t* field_type;
};

struct FidlStructPadding {
  struct FidlStructElementHeader header;

  uint32_t offset;
  // Masks with 0xff on bytes with padding and 0x00 otherwsie.
  // They are used by VisitInternalPadding to zero (encoding) and validate (decoding)
  // padding bytes.
  union {
    uint16_t mask_16;
    uint32_t mask_32;
    uint64_t mask_64;
  };
};

// A struct element is either a field or padding.
struct FidlStructElement {
  union {
    struct FidlStructElementHeader header;
    struct FidlStructField field;
    struct FidlStructPadding padding;
  };

#ifdef __cplusplus
  static constexpr FidlStructElement Field(const fidl_type* type, uint32_t offset,
                                           FidlIsResource is_resource) {
    return FidlStructElement{
        .field =
            FidlStructField{
                .header =
                    FidlStructElementHeader{
                        .element_type = kFidlStructElementType_Field,
                        .is_resource = is_resource,
                    },
                .offset = offset,
                .field_type = type,
            },
    };
  }
  static constexpr FidlStructElement Padding64(uint32_t offset, uint64_t mask) {
    return FidlStructElement{
        .padding =
            FidlStructPadding{
                .header =
                    FidlStructElementHeader{
                        .element_type = kFidlStructElementType_Padding64,
                        .is_resource = kFidlIsResource_NotResource,
                    },
                .offset = offset,
                .mask_64 = mask,
            },
    };
  }
  static constexpr FidlStructElement Padding32(uint32_t offset, uint32_t mask) {
    return FidlStructElement{
        .padding =
            FidlStructPadding{
                .header =
                    FidlStructElementHeader{
                        .element_type = kFidlStructElementType_Padding32,
                        .is_resource = kFidlIsResource_NotResource,
                    },
                .offset = offset,
                .mask_32 = mask,
            },
    };
  }
  static constexpr FidlStructElement Padding16(uint32_t offset, uint16_t mask) {
    return FidlStructElement{
        .padding =
            FidlStructPadding{
                .header =
                    FidlStructElementHeader{
                        .element_type = kFidlStructElementType_Padding16,
                        .is_resource = kFidlIsResource_NotResource,
                    },
                .offset = offset,
                .mask_16 = mask,
            },
    };
  }
#endif  // __cplusplus
};

struct FidlTableField {
  const fidl_type_t* type;
  uint32_t ordinal;
};

struct FidlXUnionField {
  const fidl_type_t* type;
};

// TODO(fxb/42793): Consider starting enum values for FidlTypeTag from 1, not 0.
typedef uint8_t FidlTypeTag;
static const uint8_t kFidlTypePrimitive = 0;
static const uint8_t kFidlTypeEnum = 1;
static const uint8_t kFidlTypeBits = 2;
static const uint8_t kFidlTypeStruct = 3;
static const uint8_t kFidlTypeStructPointer = 4;
static const uint8_t kFidlTypeArray = 5;
static const uint8_t kFidlTypeString = 6;
static const uint8_t kFidlTypeHandle = 7;
static const uint8_t kFidlTypeVector = 8;
static const uint8_t kFidlTypeTable = 9;
static const uint8_t kFidlTypeXUnion = 10;

// TODO(fxb/42793): Consider starting enum values for FidlCodedPrimitive from 1, not 0.
typedef uint8_t FidlCodedPrimitiveSubtype;
static const uint8_t kFidlCodedPrimitiveSubtype_Bool = 0;
static const uint8_t kFidlCodedPrimitiveSubtype_Int8 = 1;
static const uint8_t kFidlCodedPrimitiveSubtype_Int16 = 2;
static const uint8_t kFidlCodedPrimitiveSubtype_Int32 = 3;
static const uint8_t kFidlCodedPrimitiveSubtype_Int64 = 4;
static const uint8_t kFidlCodedPrimitiveSubtype_Uint8 = 5;
static const uint8_t kFidlCodedPrimitiveSubtype_Uint16 = 6;
static const uint8_t kFidlCodedPrimitiveSubtype_Uint32 = 7;
static const uint8_t kFidlCodedPrimitiveSubtype_Uint64 = 8;
static const uint8_t kFidlCodedPrimitiveSubtype_Float32 = 9;
static const uint8_t kFidlCodedPrimitiveSubtype_Float64 = 10;

typedef bool (*EnumValidationPredicate)(uint64_t);

// Coding Table Definitions
//
// FIDL coding tables describe the layout and constraints of the messages.
// Each coding table must start with a `tag`, to identify the kind of the
// coding table at runtime. For improved convenience working with these types,
// we provide an empty C++ type `fidl_type_t`, which is inherited by the
// coding tables in C++ mode, and dispatches to one of the subclasses based
// on the tag.
//
// Coding tables are generated in C files to avoid delayed-initialization
// issues, but are meant to be consumed by C++ files such as the walker.
// Hence parts of below are ifdef'ed with C++-specific blocks.

struct FidlCodedPrimitive;
struct FidlCodedEnum;
struct FidlCodedBits;
struct FidlCodedStruct;
struct FidlCodedStructPointer;
struct FidlCodedTable;
struct FidlCodedXUnion;
struct FidlCodedArray;
struct FidlCodedHandle;
struct FidlCodedString;
struct FidlCodedVector;

#ifdef __cplusplus

// Empty struct containing helper functions for casting to derived
// coding table types. C++ empty base class optimization ensure that this
// struct shares the same starting address with any of its subclasses.
struct fidl_type {
  constexpr FidlTypeTag type_tag() const;
  constexpr const FidlCodedPrimitive& coded_primitive() const;
  constexpr const FidlCodedEnum& coded_enum() const;
  constexpr const FidlCodedBits& coded_bits() const;
  constexpr const FidlCodedStruct& coded_struct() const;
  constexpr const FidlCodedStructPointer& coded_struct_pointer() const;
  constexpr const FidlCodedTable& coded_table() const;
  constexpr const FidlCodedXUnion& coded_xunion() const;
  constexpr const FidlCodedArray& coded_array() const;
  constexpr const FidlCodedHandle& coded_handle() const;
  constexpr const FidlCodedString& coded_string() const;
  constexpr const FidlCodedVector& coded_vector() const;

 private:
  // Prevent instances of this class from being accidentally used standalone
  // as a value.
  constexpr fidl_type() = default;
};

#define FIDL_INTERNAL_INHERIT_TYPE_T \
  final:                             \
  fidl_type

// When compiling in C++14 mode, the rules around initialization
// changes such that the compiler requires an explicit constructor.
// Since coding tables are defined in C and consumed in C++,
// it is safe to delete the constructor.
// Note that this still allows the use of C++ designated initializers.
#define FIDL_INTERNAL_DELETE_DEFAULT_CONSTRUCTOR(cls) cls() = delete;

#else

// No inheritance in C mode. This is okay because inheriting
// from an empty class does not affect the object layout at all.
#define FIDL_INTERNAL_INHERIT_TYPE_T
#define FIDL_INTERNAL_DELETE_DEFAULT_CONSTRUCTOR(cls)

#endif  // __cplusplus

struct FidlCodedPrimitive FIDL_INTERNAL_INHERIT_TYPE_T {
  const FidlTypeTag tag;
  const FidlCodedPrimitiveSubtype type;

  FIDL_INTERNAL_DELETE_DEFAULT_CONSTRUCTOR(FidlCodedPrimitive)
};

struct FidlCodedEnum FIDL_INTERNAL_INHERIT_TYPE_T {
  const FidlTypeTag tag;
  const FidlCodedPrimitiveSubtype underlying_type;
  const EnumValidationPredicate validate;
  const char* name;  // may be nullptr if omitted at compile time

  FIDL_INTERNAL_DELETE_DEFAULT_CONSTRUCTOR(FidlCodedEnum)
};

struct FidlCodedBits FIDL_INTERNAL_INHERIT_TYPE_T {
  const FidlTypeTag tag;
  const FidlCodedPrimitiveSubtype underlying_type;
  const uint64_t mask;
  const char* name;  // may be nullptr if omitted at compile time

  FIDL_INTERNAL_DELETE_DEFAULT_CONSTRUCTOR(FidlCodedBits)
};

// Though the |size| is implied by the fields, computing that information is not
// the purview of this library. It's easier for the compiler to stash it.
struct FidlCodedStruct FIDL_INTERNAL_INHERIT_TYPE_T {
  const FidlTypeTag tag;
  // element_count should be a uint32_t, but for the sake of binary size
  // a uint16_t is used (all existing values fit within this size).
  // If a larger size is needed, replace FidlCodedStruct or add a second
  // variant that supports the larger size.
  const uint16_t element_count;
  const uint32_t size;
  const struct FidlStructElement* const elements;
  const char* name;  // may be nullptr if omitted at compile time

  FIDL_INTERNAL_DELETE_DEFAULT_CONSTRUCTOR(FidlCodedStruct)
};

struct FidlCodedStructPointer FIDL_INTERNAL_INHERIT_TYPE_T {
  const FidlTypeTag tag;
  const struct FidlCodedStruct* const struct_type;

  FIDL_INTERNAL_DELETE_DEFAULT_CONSTRUCTOR(FidlCodedStructPointer)
};

struct FidlCodedTable FIDL_INTERNAL_INHERIT_TYPE_T {
  const FidlTypeTag tag;
  const uint32_t field_count;
  const struct FidlTableField* const fields;
  const char* name;  // may be nullptr if omitted at compile time

  FIDL_INTERNAL_DELETE_DEFAULT_CONSTRUCTOR(FidlCodedTable)
};

struct FidlCodedXUnion FIDL_INTERNAL_INHERIT_TYPE_T {
  const FidlTypeTag tag;
  const FidlNullability nullable;
  const FidlStrictness strictness;
  const uint32_t field_count;
  // The fields are in ordinal order, with ordinal 1 at index 0.
  const struct FidlXUnionField* const fields;
  const char* name;  // may be nullptr if omitted at compile time

  FIDL_INTERNAL_DELETE_DEFAULT_CONSTRUCTOR(FidlCodedXUnion)
};

// An array is essentially a struct with |array_size / element_size| of the same field, named at
// |element|.
struct FidlCodedArray FIDL_INTERNAL_INHERIT_TYPE_T {
  const FidlTypeTag tag;
  // element_size should be a uint32_t, but for the sake of binary size
  // a uint16_t is used (all existing values fit within this size).
  // If a larger size is needed, replace FidlCodedArray or add a second
  // variant that supports the larger size.
  const uint16_t element_size;
  const uint32_t array_size;
  const fidl_type_t* const element;

  FIDL_INTERNAL_DELETE_DEFAULT_CONSTRUCTOR(FidlCodedArray)
};

struct FidlCodedHandle FIDL_INTERNAL_INHERIT_TYPE_T {
  const FidlTypeTag tag;
  const FidlNullability nullable;
  const zx_obj_type_t handle_subtype;
  const zx_rights_t handle_rights;

  static_assert(ZX_OBJ_TYPE_UPPER_BOUND <= UINT32_MAX, "");

  FIDL_INTERNAL_DELETE_DEFAULT_CONSTRUCTOR(FidlCodedHandle)
};

struct FidlCodedString FIDL_INTERNAL_INHERIT_TYPE_T {
  const FidlTypeTag tag;
  const FidlNullability nullable;
  const uint32_t max_size;

  FIDL_INTERNAL_DELETE_DEFAULT_CONSTRUCTOR(FidlCodedString)
};

// Note that |max_count * element_size| is guaranteed to fit into a uint32_t. Unlike other types,
// the |element| pointer may be null. This occurs when the element type contains no interesting bits
// (i.e. pointers or handles).
struct FidlCodedVector FIDL_INTERNAL_INHERIT_TYPE_T {
  const FidlTypeTag tag;
  const FidlNullability nullable;
  const uint32_t max_count;
  const uint32_t element_size;
  const fidl_type_t* const element;

  FIDL_INTERNAL_DELETE_DEFAULT_CONSTRUCTOR(FidlCodedVector)
};

#ifdef __cplusplus

struct FidlHasTypeTag final : fidl_type {
  const FidlTypeTag tag;

  FidlHasTypeTag() = delete;
};

constexpr FidlTypeTag fidl_type::type_tag() const {
  return static_cast<const FidlHasTypeTag*>(this)->tag;
}

constexpr const FidlCodedPrimitive& fidl_type::coded_primitive() const {
  return *static_cast<const FidlCodedPrimitive*>(this);
}

constexpr const FidlCodedEnum& fidl_type::coded_enum() const {
  return *static_cast<const FidlCodedEnum*>(this);
}

constexpr const FidlCodedBits& fidl_type::coded_bits() const {
  return *static_cast<const FidlCodedBits*>(this);
}

constexpr const FidlCodedStruct& fidl_type::coded_struct() const {
  return *static_cast<const FidlCodedStruct*>(this);
}

constexpr const FidlCodedStructPointer& fidl_type::coded_struct_pointer() const {
  return *static_cast<const FidlCodedStructPointer*>(this);
}

constexpr const FidlCodedTable& fidl_type::coded_table() const {
  return *static_cast<const FidlCodedTable*>(this);
}

constexpr const FidlCodedXUnion& fidl_type::coded_xunion() const {
  return *static_cast<const FidlCodedXUnion*>(this);
}

constexpr const FidlCodedArray& fidl_type::coded_array() const {
  return *static_cast<const FidlCodedArray*>(this);
}

constexpr const FidlCodedHandle& fidl_type::coded_handle() const {
  return *static_cast<const FidlCodedHandle*>(this);
}

constexpr const FidlCodedString& fidl_type::coded_string() const {
  return *static_cast<const FidlCodedString*>(this);
}

constexpr const FidlCodedVector& fidl_type::coded_vector() const {
  return *static_cast<const FidlCodedVector*>(this);
}

#endif  // __cplusplus

extern const struct FidlCodedPrimitive fidl_internal_kBoolTable;
extern const struct FidlCodedPrimitive fidl_internal_kInt8Table;
extern const struct FidlCodedPrimitive fidl_internal_kInt16Table;
extern const struct FidlCodedPrimitive fidl_internal_kInt32Table;
extern const struct FidlCodedPrimitive fidl_internal_kInt64Table;
extern const struct FidlCodedPrimitive fidl_internal_kUint8Table;
extern const struct FidlCodedPrimitive fidl_internal_kUint16Table;
extern const struct FidlCodedPrimitive fidl_internal_kUint32Table;
extern const struct FidlCodedPrimitive fidl_internal_kUint64Table;
extern const struct FidlCodedPrimitive fidl_internal_kFloat32Table;
extern const struct FidlCodedPrimitive fidl_internal_kFloat64Table;

__END_CDECLS

#ifdef __cplusplus
// All the data in coding tables should be pure data.
static_assert(std::is_standard_layout<FidlTypeTag>::value, "");
static_assert(std::is_standard_layout<FidlStructField>::value, "");
static_assert(std::is_standard_layout<FidlTableField>::value, "");
static_assert(std::is_standard_layout<FidlCodedStruct>::value, "");
static_assert(std::is_standard_layout<FidlCodedStructPointer>::value, "");
static_assert(std::is_standard_layout<FidlCodedXUnion>::value, "");
static_assert(std::is_standard_layout<FidlCodedArray>::value, "");
static_assert(std::is_standard_layout<FidlCodedVector>::value, "");
static_assert(std::is_standard_layout<FidlCodedString>::value, "");
static_assert(std::is_standard_layout<FidlCodedHandle>::value, "");
static_assert(std::is_standard_layout<FidlStructElement>::value, "");
#endif  // __cplusplus

static_assert(offsetof(struct FidlCodedStruct, tag) == 0, "");
static_assert(offsetof(struct FidlCodedStructPointer, tag) == 0, "");
static_assert(offsetof(struct FidlCodedXUnion, tag) == 0, "");
static_assert(offsetof(struct FidlCodedArray, tag) == 0, "");
static_assert(offsetof(struct FidlCodedVector, tag) == 0, "");
static_assert(offsetof(struct FidlCodedString, tag) == 0, "");
static_assert(offsetof(struct FidlCodedHandle, tag) == 0, "");

// Take caution when increasing the size numbers below. While they
// can be changed as needed when the structure evolves, these growing
// has a large impact on binary size and memory footprint.

static_assert(sizeof(struct FidlCodedPrimitive) == 2, "");
static_assert(sizeof(struct FidlCodedEnum) == 24, "");
static_assert(sizeof(struct FidlCodedBits) == 24, "");
static_assert(sizeof(struct FidlCodedStruct) == 24, "");
static_assert(sizeof(struct FidlCodedStructPointer) == 16, "");
static_assert(sizeof(struct FidlCodedXUnion) == 24, "");
static_assert(sizeof(struct FidlCodedArray) == 16, "");
static_assert(sizeof(struct FidlCodedVector) == 24, "");
static_assert(sizeof(struct FidlCodedString) == 8, "");
static_assert(sizeof(struct FidlCodedHandle) == 12, "");

static_assert(sizeof(struct FidlStructField) == 16, "");
static_assert(sizeof(struct FidlTableField) == 16, "");
static_assert(sizeof(struct FidlXUnionField) == 8, "");

static_assert(sizeof(struct FidlStructElement) == 16, "");

#endif  // LIB_FIDL_INTERNAL_H_
