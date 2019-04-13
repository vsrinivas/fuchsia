// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_INTERNAL_H_
#define LIB_FIDL_INTERNAL_H_

#include <assert.h>
#include <cstdint>

#include <lib/fidl/coding.h>
#include <zircon/syscalls/object.h>
#include <zircon/types.h>

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

constexpr inline uint64_t FidlAlign(uint32_t offset) {
    constexpr uint64_t alignment_mask = FIDL_ALIGNMENT - 1;
    return (offset + alignment_mask) & ~alignment_mask;
}

// Determine if the pointer is aligned to |FIDL_ALIGNMENT|.
inline bool IsAligned(uint8_t* ptr) {
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
    if (add_overflow(new_offset, size, &new_offset)
        || add_overflow(new_offset, kMask, &new_offset)) {
        return false;
    }
    new_offset &= ~kMask;
    *out_offset = new_offset;
    return true;
}

struct FidlStructField {
    const fidl_type* type;
    uint32_t offset;

    constexpr FidlStructField(const fidl_type* type, uint32_t offset)
        : type(type), offset(offset) {}
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

// Though the |size| is implied by the fields, computing that information is not the purview of this
// library. It's easier for the compiler to stash it.
struct FidlCodedStruct {
    const FidlStructField* const fields;
    const uint32_t field_count;
    const uint32_t size;
    const char* name; // may be nullptr if omitted at compile time

    constexpr FidlCodedStruct(const FidlStructField* fields, uint32_t field_count, uint32_t size,
                              const char* name)
        : fields(fields), field_count(field_count), size(size), name(name) {}
};

struct FidlCodedStructPointer {
    const FidlCodedStruct* const struct_type;

    constexpr explicit FidlCodedStructPointer(const FidlCodedStruct* struct_type)
        : struct_type(struct_type) {}
};

struct FidlCodedTable {
    const FidlTableField* const fields;
    const uint32_t field_count;
    const char* name; // may be nullptr if omitted at compile time

    constexpr FidlCodedTable(const FidlTableField* fields, uint32_t field_count,
                             const char* name)
        : fields(fields), field_count(field_count), name(name) {}
};

// Unlike structs, union members do not have different offsets, so this points
// to an array of |fidl_type*| rather than |FidlStructField|.
//
// On-the-wire unions begin with a tag which is an index into |types|.
// |data_offset| is the offset of the data in the wire format (tag + padding).
struct FidlCodedUnion {
    const fidl_type* const* types;
    const uint32_t type_count;
    const uint32_t data_offset;
    const uint32_t size;
    const char* name; // may be nullptr if omitted at compile time

    constexpr FidlCodedUnion(const fidl_type* const* types, uint32_t type_count,
                             uint32_t data_offset, uint32_t size, const char* name)
        : types(types), type_count(type_count), data_offset(data_offset), size(size), name(name) {}
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
    const char* name; // may be nullptr if omitted at compile time

    constexpr FidlCodedXUnion(uint32_t field_count, const FidlXUnionField* fields,
                              FidlNullability nullable, const char* name)
        : field_count(field_count), fields(fields), nullable(nullable), name(name) {}
};

// An array is essentially a struct with |array_size / element_size| of the same field, named at
// |element|.
struct FidlCodedArray {
    const fidl_type* const element;
    const uint32_t array_size;
    const uint32_t element_size;

    constexpr FidlCodedArray(const fidl_type* element, uint32_t array_size, uint32_t element_size)
        : element(element), array_size(array_size), element_size(element_size) {}
};

// Note: must keep in sync with fidlc types.h HandleSubtype.
enum FidlHandleSubtype : zx_obj_type_t {
    // special case to indicate subtype is not specified.
    kFidlHandleSubtypeHandle = ZX_OBJ_TYPE_NONE,

    kFidlHandleSubtypeProcess = ZX_OBJ_TYPE_PROCESS,
    kFidlHandleSubtypeThread = ZX_OBJ_TYPE_THREAD,
    kFidlHandleSubtypeVmo = ZX_OBJ_TYPE_VMO,
    kFidlHandleSubtypeChannel = ZX_OBJ_TYPE_CHANNEL,
    kFidlHandleSubtypeEvent = ZX_OBJ_TYPE_EVENT,
    kFidlHandleSubtypePort = ZX_OBJ_TYPE_PORT,
    kFidlHandleSubtypeInterrupt = ZX_OBJ_TYPE_INTERRUPT,
    kFidlHandleSubtypeLog = ZX_OBJ_TYPE_LOG,
    kFidlHandleSubtypeSocket = ZX_OBJ_TYPE_SOCKET,
    kFidlHandleSubtypeResource = ZX_OBJ_TYPE_RESOURCE,
    kFidlHandleSubtypeEventpair = ZX_OBJ_TYPE_EVENTPAIR,
    kFidlHandleSubtypeJob = ZX_OBJ_TYPE_JOB,
    kFidlHandleSubtypeVmar = ZX_OBJ_TYPE_VMAR,
    kFidlHandleSubtypeFifo = ZX_OBJ_TYPE_FIFO,
    kFidlHandleSubtypeGuest = ZX_OBJ_TYPE_GUEST,
    kFidlHandleSubtypeTimer = ZX_OBJ_TYPE_TIMER,
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

    constexpr FidlCodedVector(const fidl_type* element, uint32_t max_count, uint32_t element_size,
                              FidlNullability nullable)
        : element(element), max_count(max_count), element_size(element_size), nullable(nullable) {}
};

} // namespace fidl

struct fidl_type {
    const fidl::FidlTypeTag type_tag;
    const union {
        const fidl::FidlCodedPrimitive coded_primitive;
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

} // namespace internal

} // namespace fidl

#endif // LIB_FIDL_INTERNAL_H_
