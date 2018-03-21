// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/type_support.h>

#include <stdalign.h>
#include <lib/fidl/coding.h>

// Handle types.
struct nonnullable_handle_inline_data {
    alignas(FIDL_ALIGNMENT)
    fidl_message_header_t header;
    zx_handle_t handle;
};
struct nonnullable_handle_message_layout {
    alignas(FIDL_ALIGNMENT)
    nonnullable_handle_inline_data inline_struct;
};

struct multiple_nonnullable_handles_inline_data {
    alignas(FIDL_ALIGNMENT)
    fidl_message_header_t header;
    uint32_t data_0;
    zx_handle_t handle_0;
    uint64_t data_1;
    zx_handle_t handle_1;
    zx_handle_t handle_2;
    uint64_t data_2;
};
struct multiple_nonnullable_handles_message_layout {
    alignas(FIDL_ALIGNMENT)
    multiple_nonnullable_handles_inline_data inline_struct;
};

struct nullable_handle_inline_data {
    alignas(FIDL_ALIGNMENT)
    fidl_message_header_t header;
    zx_handle_t handle;
};
struct nullable_handle_message_layout {
    alignas(FIDL_ALIGNMENT)
    nullable_handle_inline_data inline_struct;
};

struct multiple_nullable_handles_inline_data {
    alignas(FIDL_ALIGNMENT)
    fidl_message_header_t header;
    uint32_t data_0;
    zx_handle_t handle_0;
    uint64_t data_1;
    zx_handle_t handle_1;
    zx_handle_t handle_2;
    uint64_t data_2;
};
struct multiple_nullable_handles_message_layout {
    alignas(FIDL_ALIGNMENT)
    multiple_nullable_handles_inline_data inline_struct;
};

// Array types.
struct array_of_nonnullable_handles_inline_data {
    alignas(FIDL_ALIGNMENT)
    fidl_message_header_t header;
    zx_handle_t handles[4];
};
struct array_of_nonnullable_handles_message_layout {
    alignas(FIDL_ALIGNMENT)
    array_of_nonnullable_handles_inline_data inline_struct;
};

struct array_of_nullable_handles_inline_data {
    alignas(FIDL_ALIGNMENT)
    fidl_message_header_t header;
    zx_handle_t handles[5];
};
struct array_of_nullable_handles_message_layout {
    alignas(FIDL_ALIGNMENT)
    array_of_nullable_handles_inline_data inline_struct;
};

struct array_of_array_of_nonnullable_handles_inline_data {
    alignas(FIDL_ALIGNMENT)
    fidl_message_header_t header;
    zx_handle_t handles[3][4];
};
struct array_of_array_of_nonnullable_handles_message_layout {
    alignas(FIDL_ALIGNMENT)
    array_of_array_of_nonnullable_handles_inline_data inline_struct;
};

struct array_of_nonnullable_handles {
    alignas(FIDL_ALIGNMENT)
    zx_handle_t handles[4];
};
struct out_of_line_array_of_nonnullable_handles_inline_data {
    alignas(FIDL_ALIGNMENT)
    fidl_message_header_t header;
    array_of_nonnullable_handles* maybe_array;
};
struct out_of_line_array_of_nonnullable_handles_message_layout {
    alignas(FIDL_ALIGNMENT)
    out_of_line_array_of_nonnullable_handles_inline_data inline_struct;
    alignas(FIDL_ALIGNMENT) array_of_nonnullable_handles data;
};

// String types.
struct unbounded_nonnullable_string_inline_data {
    alignas(FIDL_ALIGNMENT)
    fidl_message_header_t header;
    fidl_string_t string;
};
struct unbounded_nonnullable_string_message_layout {
    alignas(FIDL_ALIGNMENT)
    unbounded_nonnullable_string_inline_data inline_struct;
    alignas(FIDL_ALIGNMENT) char data[6];
};

struct unbounded_nullable_string_inline_data {
    alignas(FIDL_ALIGNMENT)
    fidl_message_header_t header;
    fidl_string_t string;
};
struct unbounded_nullable_string_message_layout {
    alignas(FIDL_ALIGNMENT)
    unbounded_nullable_string_inline_data inline_struct;
    alignas(FIDL_ALIGNMENT) char data[6];
};

struct bounded_32_nonnullable_string_inline_data {
    alignas(FIDL_ALIGNMENT)
    fidl_message_header_t header;
    fidl_string_t string;
};
struct bounded_32_nonnullable_string_message_layout {
    alignas(FIDL_ALIGNMENT)
    bounded_32_nonnullable_string_inline_data inline_struct;
    alignas(FIDL_ALIGNMENT) char data[6];
};

struct bounded_32_nullable_string_inline_data {
    alignas(FIDL_ALIGNMENT)
    fidl_message_header_t header;
    fidl_string_t string;
};
struct bounded_32_nullable_string_message_layout {
    alignas(FIDL_ALIGNMENT)
    bounded_32_nullable_string_inline_data inline_struct;
    alignas(FIDL_ALIGNMENT) char data[6];
};

struct multiple_nonnullable_strings_inline_data {
    alignas(FIDL_ALIGNMENT)
    fidl_message_header_t header;
    fidl_string_t string;
    fidl_string_t string2;
};
struct multiple_nonnullable_strings_message_layout {
    alignas(FIDL_ALIGNMENT)
    multiple_nonnullable_strings_inline_data inline_struct;
    alignas(FIDL_ALIGNMENT) char data[6];
    alignas(FIDL_ALIGNMENT) char data2[8];
};

struct multiple_nullable_strings_inline_data {
    alignas(FIDL_ALIGNMENT)
    fidl_message_header_t header;
    fidl_string_t string;
    fidl_string_t string2;
};
struct multiple_nullable_strings_message_layout {
    alignas(FIDL_ALIGNMENT)
    multiple_nullable_strings_inline_data inline_struct;
    alignas(FIDL_ALIGNMENT) char data[6];
    alignas(FIDL_ALIGNMENT) char data2[8];
};

struct multiple_short_nonnullable_strings_inline_data {
    alignas(FIDL_ALIGNMENT)
    fidl_message_header_t header;
    fidl_string_t string;
    fidl_string_t string2;
};
struct multiple_short_nonnullable_strings_message_layout {
    alignas(FIDL_ALIGNMENT)
    multiple_short_nonnullable_strings_inline_data inline_struct;
    alignas(FIDL_ALIGNMENT) char data[6];
    alignas(FIDL_ALIGNMENT) char data2[8];
};

struct multiple_short_nullable_strings_inline_data {
    alignas(FIDL_ALIGNMENT)
    fidl_message_header_t header;
    fidl_string_t string;
    fidl_string_t string2;
};
struct multiple_short_nullable_strings_message_layout {
    alignas(FIDL_ALIGNMENT)
    multiple_short_nullable_strings_inline_data inline_struct;
    alignas(FIDL_ALIGNMENT) char data[6];
    alignas(FIDL_ALIGNMENT) char data2[8];
};

// Vector types.
struct unbounded_nonnullable_vector_of_handles_inline_data {
    alignas(FIDL_ALIGNMENT)
    fidl_message_header_t header;
    fidl_vector_t vector;
};
struct unbounded_nonnullable_vector_of_handles_message_layout {
    alignas(FIDL_ALIGNMENT)
    unbounded_nonnullable_vector_of_handles_inline_data inline_struct;
    alignas(FIDL_ALIGNMENT) zx_handle_t handles[4];
};

struct unbounded_nullable_vector_of_handles_inline_data {
    alignas(FIDL_ALIGNMENT)
    fidl_message_header_t header;
    fidl_vector_t vector;
};
struct unbounded_nullable_vector_of_handles_message_layout {
    alignas(FIDL_ALIGNMENT)
    unbounded_nullable_vector_of_handles_inline_data inline_struct;
    alignas(FIDL_ALIGNMENT) zx_handle_t handles[4];
};

struct bounded_32_nonnullable_vector_of_handles_inline_data {
    alignas(FIDL_ALIGNMENT)
    fidl_message_header_t header;
    fidl_vector_t vector;
};
struct bounded_32_nonnullable_vector_of_handles_message_layout {
    alignas(FIDL_ALIGNMENT)
    bounded_32_nonnullable_vector_of_handles_inline_data inline_struct;
    alignas(FIDL_ALIGNMENT) zx_handle_t handles[4];
};

struct bounded_32_nullable_vector_of_handles_inline_data {
    alignas(FIDL_ALIGNMENT)
    fidl_message_header_t header;
    fidl_vector_t vector;
};
struct bounded_32_nullable_vector_of_handles_message_layout {
    alignas(FIDL_ALIGNMENT)
    bounded_32_nullable_vector_of_handles_inline_data inline_struct;
    alignas(FIDL_ALIGNMENT) zx_handle_t handles[4];
};

struct multiple_nonnullable_vectors_of_handles_inline_data {
    alignas(FIDL_ALIGNMENT)
    fidl_message_header_t header;
    fidl_vector_t vector;
    fidl_vector_t vector2;
};
struct multiple_nonnullable_vectors_of_handles_message_layout {
    alignas(FIDL_ALIGNMENT)
    multiple_nonnullable_vectors_of_handles_inline_data inline_struct;
    alignas(FIDL_ALIGNMENT) zx_handle_t handles[4];
    alignas(FIDL_ALIGNMENT) zx_handle_t handles2[4];
};

struct multiple_nullable_vectors_of_handles_inline_data {
    alignas(FIDL_ALIGNMENT)
    fidl_message_header_t header;
    fidl_vector_t vector;
    fidl_vector_t vector2;
};
struct multiple_nullable_vectors_of_handles_message_layout {
    alignas(FIDL_ALIGNMENT)
    multiple_nullable_vectors_of_handles_inline_data inline_struct;
    alignas(FIDL_ALIGNMENT) zx_handle_t handles[4];
    alignas(FIDL_ALIGNMENT) zx_handle_t handles2[4];
};

struct unbounded_nonnullable_vector_of_uint32_inline_data {
    alignas(FIDL_ALIGNMENT)
    fidl_message_header_t header;
    fidl_vector_t vector;
};
struct unbounded_nonnullable_vector_of_uint32_message_layout {
    alignas(FIDL_ALIGNMENT)
    unbounded_nonnullable_vector_of_uint32_inline_data inline_struct;
    alignas(FIDL_ALIGNMENT) uint32_t uint32[4];
};

struct unbounded_nullable_vector_of_uint32_inline_data {
    alignas(FIDL_ALIGNMENT)
    fidl_message_header_t header;
    fidl_vector_t vector;
};
struct unbounded_nullable_vector_of_uint32_message_layout {
    alignas(FIDL_ALIGNMENT)
    unbounded_nullable_vector_of_uint32_inline_data inline_struct;
    alignas(FIDL_ALIGNMENT) uint32_t uint32[4];
};

struct bounded_32_nonnullable_vector_of_uint32_inline_data {
    alignas(FIDL_ALIGNMENT)
    fidl_message_header_t header;
    fidl_vector_t vector;
};
struct bounded_32_nonnullable_vector_of_uint32_message_layout {
    alignas(FIDL_ALIGNMENT)
    bounded_32_nonnullable_vector_of_uint32_inline_data inline_struct;
    alignas(FIDL_ALIGNMENT) uint32_t uint32[4];
};

struct bounded_32_nullable_vector_of_uint32_inline_data {
    alignas(FIDL_ALIGNMENT)
    fidl_message_header_t header;
    fidl_vector_t vector;
};
struct bounded_32_nullable_vector_of_uint32_message_layout {
    alignas(FIDL_ALIGNMENT)
    bounded_32_nullable_vector_of_uint32_inline_data inline_struct;
    alignas(FIDL_ALIGNMENT) uint32_t uint32[4];
};

struct multiple_nonnullable_vectors_of_uint32_inline_data {
    alignas(FIDL_ALIGNMENT)
    fidl_message_header_t header;
    fidl_vector_t vector;
    fidl_vector_t vector2;
};
struct multiple_nonnullable_vectors_of_uint32_message_layout {
    alignas(FIDL_ALIGNMENT)
    multiple_nonnullable_vectors_of_uint32_inline_data inline_struct;
    alignas(FIDL_ALIGNMENT) uint32_t uint32[4];
    alignas(FIDL_ALIGNMENT) uint32_t uint32_2[4];
};

struct multiple_nullable_vectors_of_uint32_inline_data {
    alignas(FIDL_ALIGNMENT)
    fidl_message_header_t header;
    fidl_vector_t vector;
    fidl_vector_t vector2;
};
struct multiple_nullable_vectors_of_uint32_message_layout {
    alignas(FIDL_ALIGNMENT)
    multiple_nullable_vectors_of_uint32_inline_data inline_struct;
    alignas(FIDL_ALIGNMENT) uint32_t uint32[4];
    alignas(FIDL_ALIGNMENT) uint32_t uint32_2[4];
};

// Union types.
#define nonnullable_handle_union_kHandle UINT32_C(0)
struct nonnullable_handle_union {
    alignas(FIDL_ALIGNMENT)
    fidl_union_tag_t tag;
    union {
        zx_handle_t handle;
    };
};
struct nonnullable_handle_union_inline_data {
    alignas(FIDL_ALIGNMENT)
    fidl_message_header_t header;
    nonnullable_handle_union data;
};
struct nonnullable_handle_union_message_layout {
    alignas(FIDL_ALIGNMENT)
    nonnullable_handle_union_inline_data inline_struct;
};

#define array_of_nonnullable_handles_union_kHandle UINT32_C(0)
#define array_of_nonnullable_handles_union_kArrayOfHandles UINT32_C(1)
#define array_of_nonnullable_handles_union_kArrayOfArrayOfHandles UINT32_C(2)
struct array_of_nonnullable_handles_union {
    alignas(FIDL_ALIGNMENT)
    fidl_union_tag_t tag;
    union {
        zx_handle_t handle;
        zx_handle_t array_of_handles[2];
        zx_handle_t array_of_array_of_handles[2][2];
    };
};
struct array_of_nonnullable_handles_union_inline_data {
    alignas(FIDL_ALIGNMENT)
    fidl_message_header_t header;
    array_of_nonnullable_handles_union data;
};
struct array_of_nonnullable_handles_union_message_layout {
    alignas(FIDL_ALIGNMENT)
    array_of_nonnullable_handles_union_inline_data inline_struct;
};

// Union pointer types.
struct nonnullable_handle_union_ptr_inline_data {
    alignas(FIDL_ALIGNMENT)
    fidl_message_header_t header;
    nonnullable_handle_union* data;
};
struct nonnullable_handle_union_ptr_message_layout {
    alignas(FIDL_ALIGNMENT)
    nonnullable_handle_union_ptr_inline_data inline_struct;
    alignas(FIDL_ALIGNMENT) nonnullable_handle_union data;
};

struct array_of_nonnullable_handles_union_ptr_inline_data {
    alignas(FIDL_ALIGNMENT)
    fidl_message_header_t header;
    array_of_nonnullable_handles_union* data;
};
struct array_of_nonnullable_handles_union_ptr_message_layout {
    alignas(FIDL_ALIGNMENT)
    array_of_nonnullable_handles_union_ptr_inline_data inline_struct;
    alignas(FIDL_ALIGNMENT) array_of_nonnullable_handles_union data;
};

// Struct alignas(FIDL_ALIGNMENT) types.
struct struct_level_3 {
    alignas(FIDL_ALIGNMENT)
    uint32_t padding_3;
    zx_handle_t handle_3;
};
struct struct_level_2 {
    alignas(FIDL_ALIGNMENT)
    uint64_t padding_2;
    struct_level_3 l3;
    zx_handle_t handle_2;
};
struct struct_level_1 {
    alignas(FIDL_ALIGNMENT)
    zx_handle_t handle_1;
    struct_level_2 l2;
    uint64_t padding_1;
};
struct struct_level_0 {
    alignas(FIDL_ALIGNMENT)
    uint64_t padding_0;
    struct_level_1 l1;
    zx_handle_t handle_0;
};
struct nested_structs_inline_data {
    alignas(FIDL_ALIGNMENT)
    fidl_message_header_t header;
    struct_level_0 l0;
};
struct nested_structs_message_layout {
    alignas(FIDL_ALIGNMENT)
    nested_structs_inline_data inline_struct;
};

// Struct alignas(FIDL_ALIGNMENT) pointer types.
struct struct_ptr_level_3 {
    alignas(FIDL_ALIGNMENT)
    uint32_t padding_3;
    zx_handle_t handle_3;
};
struct struct_ptr_level_2 {
    alignas(FIDL_ALIGNMENT)
    uint64_t padding_2;
    struct_ptr_level_3* l3_present;
    struct_ptr_level_3* l3_absent;
    struct_ptr_level_3 l3_inline;
    zx_handle_t handle_2;
};
struct struct_ptr_level_1 {
    alignas(FIDL_ALIGNMENT)
    zx_handle_t handle_1;
    struct_ptr_level_2* l2_present;
    struct_ptr_level_2 l2_inline;
    struct_ptr_level_2* l2_absent;
    uint64_t padding_1;
};
struct struct_ptr_level_0 {
    alignas(FIDL_ALIGNMENT)
    uint64_t padding_0;
    struct_ptr_level_1* l1_absent;
    struct_ptr_level_1 l1_inline;
    zx_handle_t handle_0;
    struct_ptr_level_1* l1_present;
};
struct nested_struct_ptrs_inline_data {
    alignas(FIDL_ALIGNMENT)
    fidl_message_header_t header;
    struct_ptr_level_0 l0_inline;
    struct_ptr_level_0* l0_absent;
    struct_ptr_level_0* l0_present;
};
struct nested_struct_ptrs_message_layout {
    alignas(FIDL_ALIGNMENT)
    nested_struct_ptrs_inline_data inline_struct;
    alignas(FIDL_ALIGNMENT) struct_ptr_level_2 in_in_out_2;
    alignas(FIDL_ALIGNMENT) struct_ptr_level_3 in_in_out_out_3;
    alignas(FIDL_ALIGNMENT) struct_ptr_level_3 in_in_in_out_3;
    alignas(FIDL_ALIGNMENT) struct_ptr_level_1 in_out_1;
    alignas(FIDL_ALIGNMENT) struct_ptr_level_2 in_out_out_2;
    alignas(FIDL_ALIGNMENT) struct_ptr_level_3 in_out_out_out_3;
    alignas(FIDL_ALIGNMENT) struct_ptr_level_3 in_out_in_out_3;
    alignas(FIDL_ALIGNMENT) struct_ptr_level_0 out_0;
    alignas(FIDL_ALIGNMENT) struct_ptr_level_2 out_in_out_2;
    alignas(FIDL_ALIGNMENT) struct_ptr_level_3 out_in_out_out_3;
    alignas(FIDL_ALIGNMENT) struct_ptr_level_3 out_in_in_out_3;
    alignas(FIDL_ALIGNMENT) struct_ptr_level_1 out_out_1;
    alignas(FIDL_ALIGNMENT) struct_ptr_level_2 out_out_out_2;
    alignas(FIDL_ALIGNMENT) struct_ptr_level_3 out_out_out_out_3;
    alignas(FIDL_ALIGNMENT) struct_ptr_level_3 out_out_in_out_3;
};

// Recursive types.
#define maybe_recurse_union_kDone UINT32_C(0)
#define maybe_recurse_union_kMore UINT32_C(1)
struct recursion_inline_data;
struct maybe_recurse {
    alignas(FIDL_ALIGNMENT)
    fidl_union_tag_t tag;
    union {
        zx_handle_t handle;
        recursion_inline_data* more;
    };
};
struct recursion_inline_data {
    alignas(FIDL_ALIGNMENT)
    fidl_message_header_t header;
    maybe_recurse inline_union;
};
struct recursion_message_layout {
    alignas(FIDL_ALIGNMENT)
    recursion_inline_data inline_struct;
    alignas(FIDL_ALIGNMENT) recursion_inline_data depth_0;
    alignas(FIDL_ALIGNMENT) recursion_inline_data depth_1;
    alignas(FIDL_ALIGNMENT) recursion_inline_data depth_2;
    alignas(FIDL_ALIGNMENT) recursion_inline_data depth_3;
    alignas(FIDL_ALIGNMENT) recursion_inline_data depth_4;
    alignas(FIDL_ALIGNMENT) recursion_inline_data depth_5;
    alignas(FIDL_ALIGNMENT) recursion_inline_data depth_6;
    alignas(FIDL_ALIGNMENT) recursion_inline_data depth_7;
    alignas(FIDL_ALIGNMENT) recursion_inline_data depth_8;
    alignas(FIDL_ALIGNMENT) recursion_inline_data depth_9;
    alignas(FIDL_ALIGNMENT) recursion_inline_data depth_10;
    alignas(FIDL_ALIGNMENT) recursion_inline_data depth_11;
    alignas(FIDL_ALIGNMENT) recursion_inline_data depth_12;
    alignas(FIDL_ALIGNMENT) recursion_inline_data depth_13;
    alignas(FIDL_ALIGNMENT) recursion_inline_data depth_14;
    alignas(FIDL_ALIGNMENT) recursion_inline_data depth_15;
    alignas(FIDL_ALIGNMENT) recursion_inline_data depth_16;
    alignas(FIDL_ALIGNMENT) recursion_inline_data depth_17;
    alignas(FIDL_ALIGNMENT) recursion_inline_data depth_18;
    alignas(FIDL_ALIGNMENT) recursion_inline_data depth_19;
    alignas(FIDL_ALIGNMENT) recursion_inline_data depth_20;
    alignas(FIDL_ALIGNMENT) recursion_inline_data depth_21;
    alignas(FIDL_ALIGNMENT) recursion_inline_data depth_22;
    alignas(FIDL_ALIGNMENT) recursion_inline_data depth_23;
    alignas(FIDL_ALIGNMENT) recursion_inline_data depth_24;
    alignas(FIDL_ALIGNMENT) recursion_inline_data depth_25;
    alignas(FIDL_ALIGNMENT) recursion_inline_data depth_26;
    alignas(FIDL_ALIGNMENT) recursion_inline_data depth_27;
    alignas(FIDL_ALIGNMENT) recursion_inline_data depth_28;
    alignas(FIDL_ALIGNMENT) recursion_inline_data depth_29;
};
