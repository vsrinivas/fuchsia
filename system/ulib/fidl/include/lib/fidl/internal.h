// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_INTERNAL_H_
#define LIB_FIDL_INTERNAL_H_

#include <assert.h>
#include <stdint.h>

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

enum FidlNullability : uint32_t {
    kNonnullable = 0u,
    kNullable = 1u,
};

inline uint64_t FidlAlign(uint32_t offset) {
    constexpr uint64_t alignment_mask = FIDL_ALIGNMENT - 1;
    return (offset + alignment_mask) & ~alignment_mask;
}

struct FidlField {
    const fidl_type* type;
    uint32_t offset;

    constexpr FidlField(const fidl_type* type, uint32_t offset)
        : type(type), offset(offset) {}
};

struct FidlTableField {
    const fidl_type* type;
    uint32_t ordinal;

    constexpr FidlTableField(const fidl_type* type, uint32_t ordinal)
        : type(type), ordinal(ordinal) {}
};

enum FidlTypeTag : uint32_t {
    kFidlTypeStruct = 0u,
    kFidlTypeStructPointer = 1u,
    kFidlTypeTable = 8u,
    kFidlTypeTablePointer = 9u,
    kFidlTypeUnion = 2u,
    kFidlTypeUnionPointer = 3u,
    kFidlTypeArray = 4u,
    kFidlTypeString = 5u,
    kFidlTypeHandle = 6u,
    kFidlTypeVector = 7u,
};

// Though the |size| is implied by the fields, computing that information is not the purview of this
// library. It's easier for the compiler to stash it.
struct FidlCodedStruct {
    const FidlField* const fields;
    const uint32_t field_count;
    const uint32_t size;
    const char* name; // may be nullptr if omitted at compile time

    constexpr FidlCodedStruct(const FidlField* fields, uint32_t field_count, uint32_t size,
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

struct FidlCodedTablePointer {
    const FidlCodedTable* const table_type;

    constexpr explicit FidlCodedTablePointer(const FidlCodedTable* table_type)
        : table_type(table_type) {}
};

// Unlike structs, union members do not have different offsets, so this points to an array of
// |fidl_type*| rather than |FidlField|.
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

    static_assert(ZX_OBJ_TYPE_LAST <= UINT32_MAX, "");
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
        const fidl::FidlCodedStruct coded_struct;
        const fidl::FidlCodedStructPointer coded_struct_pointer;
        const fidl::FidlCodedTable coded_table;
        const fidl::FidlCodedTablePointer coded_table_pointer;
        const fidl::FidlCodedUnion coded_union;
        const fidl::FidlCodedUnionPointer coded_union_pointer;
        const fidl::FidlCodedHandle coded_handle;
        const fidl::FidlCodedString coded_string;
        const fidl::FidlCodedArray coded_array;
        const fidl::FidlCodedVector coded_vector;
    };

    constexpr fidl_type(fidl::FidlCodedStruct coded_struct)
        : type_tag(fidl::kFidlTypeStruct), coded_struct(coded_struct) {}

    constexpr fidl_type(fidl::FidlCodedStructPointer coded_struct_pointer)
        : type_tag(fidl::kFidlTypeStructPointer), coded_struct_pointer(coded_struct_pointer) {}

    constexpr fidl_type(fidl::FidlCodedTable coded_table)
        : type_tag(fidl::kFidlTypeTable), coded_table(coded_table) {}

    constexpr fidl_type(fidl::FidlCodedTablePointer coded_table_pointer)
        : type_tag(fidl::kFidlTypeTablePointer), coded_table_pointer(coded_table_pointer) {}

    constexpr fidl_type(fidl::FidlCodedUnion coded_union)
        : type_tag(fidl::kFidlTypeUnion), coded_union(coded_union) {}

    constexpr fidl_type(fidl::FidlCodedUnionPointer coded_union_pointer)
        : type_tag(fidl::kFidlTypeUnionPointer), coded_union_pointer(coded_union_pointer) {}

    constexpr fidl_type(fidl::FidlCodedHandle coded_handle)
        : type_tag(fidl::kFidlTypeHandle), coded_handle(coded_handle) {}

    constexpr fidl_type(fidl::FidlCodedString coded_string)
        : type_tag(fidl::kFidlTypeString), coded_string(coded_string) {}

    constexpr fidl_type(fidl::FidlCodedArray coded_array)
        : type_tag(fidl::kFidlTypeArray), coded_array(coded_array) {}

    constexpr fidl_type(fidl::FidlCodedVector coded_vector)
        : type_tag(fidl::kFidlTypeVector), coded_vector(coded_vector) {}
};

#endif // LIB_FIDL_INTERNAL_H_
