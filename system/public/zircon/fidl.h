// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <assert.h>
#include <stdalign.h>
#include <stdint.h>

#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// Fidl data types have a representation in a wire format. This wire
// format is shared by all language bindings, including C11 and C++14.
//
// The C bindings also define a representation of fidl data types. For
// a given type, the size and alignment of all parts of the type agree
// with the wire format's representation. The C representation differs
// in the representation of pointers to out-of-line allocations. On
// the wire, allocations are encoded as either present or not. In C,
// they are actual pointers. The C representation also places any
// transfered handle types (including requests) inline. The wire
// format tracks handles separately, just like the underlying channel
// transport does.
//
// Turning the wire format into the C format is called decoding.
//
// Turning the C format into the wire format is called encoding.
//
// The formats are designed to allow for in-place coding, assuming all
// out-of-line allocations placed are in traversal order (defined
// below) with natural alignment.

// Bounds.

// Various fidl types, such as strings and vectors, may be bounded. If
// no explicit bound is given, then FIDL_MAX_SIZE is implied.

#define FIDL_MAX_SIZE UINT32_MAX

// Out of line allocations.

// The fidl wire format represents potential out-of-line allocations
// (corresponding to actual pointer types in the C format) as
// uintptr_t. For allocations that are actually present and that will
// be patched up with pointers during decoding, the FIDL_ALLOC_PRESENT
// value is used. For non-present nullable allocations, the
// FIDL_ALLOC_ABSENT value is used.

#define FIDL_ALLOC_PRESENT ((uintptr_t)UINTPTR_MAX)
#define FIDL_ALLOC_ABSENT ((uintptr_t)0)

// Out of line allocations are all 8 byte aligned.
#define FIDL_ALIGNMENT ((size_t)8)
#define FIDL_ALIGN(a) (((a) + 7) & ~7)
#define FIDL_ALIGNDECL alignas(FIDL_ALIGNMENT)

// An opaque struct representing the encoding of a particular fidl
// type.
typedef struct fidl_type fidl_type_t;

// Primitive types.

// Both on the wire and once deserialized, primitive fidl types
// correspond directly to C types. There is no intermediate layer of
// typedefs. For instance, fidl's float64 is generated as double.

// All primitive types are non-nullable.

// All primitive types are naturally sized and aligned on the wire.

// fidl     C         Meaning.
// ---------------------------------------------
// bool     bool      A boolean.
// int8     int8_t    An 8 bit signed integer.
// int16    int16_t   A 16 bit signed integer.
// int32    int32_t   A 32 bit signed integer.
// int64    int64_t   A 64 bit signed integer.
// uint8    uint8_t   An 8 bit unsigned integer.
// uint16   uint16_t  A 16 bit unsigned integer.
// uint32   uint32_t  A 32 bit unsigned integer.
// uint64   uint64_t  A 64 bit unsigned integer.
// float32  float     A 32 bit IEEE-754 float.
// float64  double    A 64 bit IEEE-754 float.

// Enums.

// Fidl enums have an undering integer type (one of int8, int16,
// int32, int64, uint8, uint16, uint32, or uint64). The wire format of
// an enum and the C format of an enum are the same as the
// corresponding primitive type.

// String types.

// Fidl strings are variable-length UTF-8 strings. Strings can be
// nullable (string?) or nonnullable (string); if nullable, the null
// string is distinct from the empty string. Strings can be bounded to
// a fixed byte length (e.g. string:40? is a nullable string of at
// most 40 bytes).

// Strings are not guaranteed to be nul terminated. Strings can
// contain embedded nuls throughout their length.

// The fidl wire format dictates that strings are valid UTF-8. It is
// up to clients to provide well-formed UTF-8 and servers to check for
// it. Message encoding and decoding can, but does not by default,
// perform this check.

// All deserialized string types are represented by the fidl_string_t
// structure. This structure consists of a size (in bytes) and a
// pointer to an out-of-line allocation of uint8_t, guaranteed to be
// at least as long as the length.

// The bound on a string type is not present in the serialized format,
// but is checked as part of validation.

typedef struct {
    uint64_t size;
    char* data;
} fidl_string_t;

// When encoded, an absent nullable string is represented as a
// fidl_string_t with size 0 and FIDL_ALLOC_ABSENT data, with no
// out-of-line allocation associated with it. A present string
// (nullable or not) is represented as a fidl_string_t with some size
// and with data equal to FIDL_ALLOC_PRESENT, which the decoding
// process replaces with an actual pointer to the next out-of-line
// allocation.

// All string types:

// fidl       C              Meaning
// -----------------------------------------------------------------
// string     fidl_string_t  A string of arbitrary length.
// string?    fidl_string_t  An optional string of arbitrary length.
// string:N   fidl_string_t  A string up to N bytes long.
// string:N?  fidl_string_t  An optional string up to N bytes long.

// Arrays.

// On the wire, an array of N objects of type T (array<T, N>) is
// represented the same as N contiguous Ts. Equivalently, it is
// represented the same as a nonnullable struct containing N fields
// all of type T.

// In C, this is just represented as a C array of the corresponding C
// type.

// Vector types.

// Fidl vectors are variable-length arrays of a given type T. Vectors
// can be nullable (vector<T>?) or nonnullable (vector<T>); if
// nullable, the null vector is distinct from the empty
// vector. Vectors can be bounded to a fixed element length
// (e.g. vector<T>:40? is a nullable vector of at most 40 Ts).

// All deserialized vector types are represented by the fidl_vector_t
// structure. This structure consists of a count and a pointer to the
// bytes.

// The bound on a vector type is not present in the serialized format,
// but is checked as part of validation.

typedef struct {
    uint64_t count;
    void* data;
} fidl_vector_t;

// When encoded, an absent nullable vector is represented as a
// fidl_vector_t with size 0 and FIDL_ALLOC_ABSENT data, with no
// out-of-line allocation associated with it. A present vector
// (nullable or not) is represented as a fidl_vector_t with some size
// and with data equal to FIDL_ALLOC_PRESENT, which the decoding
// process replaces with an actual pointer to the next out-of-line
// allocation.

// All vector types:

// fidl          C              Meaning
// --------------------------------------------------------------------------
// vector<T>     fidl_vector_t  A vector of T, of arbitrary length.
// vector<T>?    fidl_vector_t  An optional vector of T, of arbitrary length.
// vector<T>:N   fidl_vector_t  A vector of T, up to N elements.
// vector<T>:N?  fidl_vector_t  An optional vector of T,  up to N elements.

// Handle types.

// Handle types are encoded directly. Just like primitive types, there
// is no fidl-specific handle type. Generated fidl structures simply
// mention zx_handle_t.

// Handle types are either nullable (handle?), or not (handle); and
// either explicitly typed (e.g. handle<Channel> or handle<Job>), or
// not.

// All fidl handle types, regardless of subtype, are represented as
// zx_handle_t. The encoding tables do know the handle subtypes,
// however, for clients which wish to perform explicit checking.

// The following are the possible handle subtypes.

// process
// thread
// vmo
// channel
// event
// port
// interrupt
// iomap
// pci
// log
// socket
// resource
// eventpair
// job
// vmar
// fifo
// hypervisor
// guest
// timer

// All handle types are 4 byte sized and aligned on the wire.

// When encoded, absent nullable handles are represented as
// FIDL_HANDLE_ABSENT. Present handles, whether nullable or not, are
// represented as FIDL_HANDLE_PRESENT, which the decoding process will
// overwrite with the next handle value in the channel message.

#define FIDL_HANDLE_ABSENT ((zx_handle_t)ZX_HANDLE_INVALID)
#define FIDL_HANDLE_PRESENT ((zx_handle_t)UINT32_MAX)

// fidl        C            Meaning
// ------------------------------------------------------------------
// handle      zx_handle_t  Any valid handle.
// handle?     zx_handle_t  Any valid handle, or ZX_HANDLE_INVALID.
// handle<T>   zx_handle_t  Any valid T handle.
// handle<T>?  zx_handle_t  Any valid T handle, or ZX_HANDLE_INVALID.

// Unions.

// Fidl unions are a tagged sum type. The tag is a 4 bytes. For every
// union type, the fidl compiler generates an enum representing the
// different variants of the enum. This is followed, in C and on the
// wire, by large enough and aligned enough storage for all members of
// the union.

// Unions may be nullable. Nullable unions are represented as a
// pointer to an out of line allocation of tag-and-member. As with
// other out-of-line allocations, ones present on the wire take the
// value FIDL_ALLOC_PRESENT and those that are not are represented by
// FIDL_ALLOC_NULL. Nonnullable unions are represented inline as a
// tag-and-member.

// For each fidl union type, a corresponding C type is generated. They
// are all structs consisting of a fidl_union_tag_t discriminant,
// followed by an anonymous union of all the union members.

typedef uint32_t fidl_union_tag_t;

// fidl                 C                            Meaning
// --------------------------------------------------------------------
// union foo {...}      struct union_foo {           An inline union.
//                          fidl_union_tag_t tag;
//                          union {...};
//                      }
//
// union foo {...}?     struct union_foo*            A pointer to a
//                                                   union_foo, or else
//                                                   FIDL_ALLOC_ABSENT.

// Messages.

// All fidl messages share a common 16 byte header.

typedef struct {
    zx_txid_t txid;
    uint32_t reserved0;
    uint32_t flags;
    uint32_t ordinal;
} fidl_message_header_t;

// Messages which do not have a response use zero as a special
// transaction id.

#define FIDL_TXID_NO_RESPONSE 0ul

// The system reserves the high half of the ordinal space.

#define FIDL_ORD_SYSTEM_MASK 0x80000000ul

// A FIDL message.
typedef struct fidl_msg {
    // The bytes of the message.
    //
    // The bytes of the message might be in the encoded or decoded form.
    // Functions that take a |fidl_msg_t| as an argument should document whether
    // the expect encoded or decoded messages.
    //
    // See |num_bytes| for the number of bytes in the message.
    void* bytes;

    // The handles of the message.
    //
    // See |num_bytes| for the number of bytes in the message.
    zx_handle_t* handles;

    // The number of bytes in |bytes|.
    uint32_t num_bytes;

    // The number of handles in |handles|.
    uint32_t num_handles;
} fidl_msg_t;

// An outstanding FIDL transaction.
typedef struct fidl_txn fidl_txn_t;
struct fidl_txn {
    // Replies to the outstanding request and complete the FIDL transaction.
    //
    // Pass the |fidl_txn_t| object itself as the first paramter. The |msg|
    // should already be encoded. This function always consumes any handles
    // present in |msg|.
    //
    // Call |reply| only once for each |txn| object. After |reply| returns, the
    // |txn| object is considered invalid and might have been freed or reused
    // for another purpose.
    zx_status_t (*reply)(fidl_txn_t* txn, const fidl_msg_t* msg);
};

// Assumptions.

// Ensure that FIDL_ALIGNMENT is sufficient.
static_assert(alignof(bool) <= FIDL_ALIGNMENT, "");
static_assert(alignof(int8_t) <= FIDL_ALIGNMENT, "");
static_assert(alignof(int16_t) <= FIDL_ALIGNMENT, "");
static_assert(alignof(int32_t) <= FIDL_ALIGNMENT, "");
static_assert(alignof(int64_t) <= FIDL_ALIGNMENT, "");
static_assert(alignof(uint8_t) <= FIDL_ALIGNMENT, "");
static_assert(alignof(uint16_t) <= FIDL_ALIGNMENT, "");
static_assert(alignof(uint32_t) <= FIDL_ALIGNMENT, "");
static_assert(alignof(uint64_t) <= FIDL_ALIGNMENT, "");
static_assert(alignof(float) <= FIDL_ALIGNMENT, "");
static_assert(alignof(double) <= FIDL_ALIGNMENT, "");
static_assert(alignof(void*) <= FIDL_ALIGNMENT, "");
static_assert(alignof(fidl_union_tag_t) <= FIDL_ALIGNMENT, "");
static_assert(alignof(fidl_message_header_t) <= FIDL_ALIGNMENT, "");

__END_CDECLS
