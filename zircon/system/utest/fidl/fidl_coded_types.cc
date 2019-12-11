// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl_coded_types.h"

#include <lib/fidl/internal.h>

#include "fidl_structs.h"

namespace {

// All sizes in fidl encoding tables are 32 bits. The fidl compiler
// normally enforces this. Check manually in manual tests.
template <typename T, size_t N>
uint32_t ArrayCount(T const (&array)[N]) {
  static_assert(N < UINT32_MAX, "Array is too large!");
  return N;
}

}  // namespace

// Handle types.
const fidl_type_t nonnullable_handle = {
    .type_tag = kFidlTypeHandle,
    {.coded_handle = {.handle_subtype = ZX_OBJ_TYPE_NONE,
                      .nullable = kFidlNullability_Nonnullable}}};
const fidl_type_t nullable_handle = {
    .type_tag = kFidlTypeHandle,
    {.coded_handle = {.handle_subtype = ZX_OBJ_TYPE_NONE, .nullable = kFidlNullability_Nullable}}};
const fidl_type_t nullable_channel_handle = {
    .type_tag = kFidlTypeHandle,
    {.coded_handle = {.handle_subtype = ZX_OBJ_TYPE_CHANNEL,
                      .nullable = kFidlNullability_Nullable}}};
const fidl_type_t nullable_vmo_handle = {
    .type_tag = kFidlTypeHandle,
    {.coded_handle = {.handle_subtype = ZX_OBJ_TYPE_VMO, .nullable = kFidlNullability_Nullable}}};
const fidl_type_t nonnullable_channel_handle = {
    .type_tag = kFidlTypeHandle,
    {.coded_handle = {.handle_subtype = ZX_OBJ_TYPE_CHANNEL,
                      .nullable = kFidlNullability_Nonnullable}}};
const fidl_type_t nonnullable_vmo_handle = {
    .type_tag = kFidlTypeHandle,
    {.coded_handle = {.handle_subtype = ZX_OBJ_TYPE_VMO,
                      .nullable = kFidlNullability_Nonnullable}}};

// Array types.
const fidl_type_t array_of_two_nonnullable_handles = {
    .type_tag = kFidlTypeArray,
    {.coded_array = {.element = &nonnullable_handle,
                     .array_size = 2 * sizeof(zx_handle_t),
                     .element_size = sizeof(zx_handle_t),
                     .alt_type = nullptr}}};
const fidl_type_t array_of_four_nonnullable_handles = {
    .type_tag = kFidlTypeArray,
    {.coded_array = {.element = &nonnullable_handle,
                     .array_size = 4 * sizeof(zx_handle_t),
                     .element_size = sizeof(zx_handle_t),
                     .alt_type = nullptr}}};
const fidl_type_t array_of_five_nullable_handles = {
    .type_tag = kFidlTypeArray,
    {.coded_array = {.element = &nullable_handle,
                     .array_size = 5 * sizeof(zx_handle_t),
                     .element_size = sizeof(zx_handle_t),
                     .alt_type = nullptr}}};
const fidl_type_t array_of_three_arrays_of_four_nonnullable_handles{
    .type_tag = kFidlTypeArray,
    {.coded_array = {.element = &array_of_four_nonnullable_handles,
                     .array_size = 3 * 4 * sizeof(zx_handle_t),
                     .element_size = 4 * sizeof(zx_handle_t),
                     .alt_type = nullptr}}};
const fidl_type_t array_of_two_arrays_of_two_nonnullable_handles = {
    .type_tag = kFidlTypeArray,
    {.coded_array = {.element = &array_of_two_nonnullable_handles,
                     .array_size = 2 * 2 * sizeof(zx_handle_t),
                     .element_size = 2 * sizeof(zx_handle_t),
                     .alt_type = nullptr}}};

// String types.
const fidl_type_t unbounded_nonnullable_string = {
    .type_tag = kFidlTypeString,
    {.coded_string = {.max_size = FIDL_MAX_SIZE, .nullable = kFidlNullability_Nonnullable}}};
const fidl_type_t unbounded_nullable_string = {
    .type_tag = kFidlTypeString,
    {.coded_string = {.max_size = FIDL_MAX_SIZE, .nullable = kFidlNullability_Nullable}}};
const fidl_type_t bounded_32_nonnullable_string = {
    .type_tag = kFidlTypeString,
    {.coded_string = {.max_size = 32, .nullable = kFidlNullability_Nonnullable}}};
const fidl_type_t bounded_32_nullable_string = {
    .type_tag = kFidlTypeString,
    {.coded_string = {.max_size = 32, .nullable = kFidlNullability_Nullable}}};
const fidl_type_t bounded_4_nonnullable_string = {
    .type_tag = kFidlTypeString,
    {.coded_string = {.max_size = 4, .nullable = kFidlNullability_Nonnullable}}};
const fidl_type_t bounded_4_nullable_string = {
    .type_tag = kFidlTypeString,
    {.coded_string = {.max_size = 4, .nullable = kFidlNullability_Nullable}}};

// Vector types.
const fidl_type_t unbounded_nonnullable_vector_of_handles = {
    .type_tag = kFidlTypeVector,
    {.coded_vector = {.element = &nonnullable_handle,
                      .max_count = FIDL_MAX_SIZE,
                      .element_size = sizeof(zx_handle_t),
                      .nullable = kFidlNullability_Nonnullable,
                      .alt_type = nullptr}}};
const fidl_type_t unbounded_nullable_vector_of_handles = {
    .type_tag = kFidlTypeVector,
    {.coded_vector = {.element = &nonnullable_handle,
                      .max_count = FIDL_MAX_SIZE,
                      .element_size = sizeof(zx_handle_t),
                      .nullable = kFidlNullability_Nullable,
                      .alt_type = nullptr}}};
const fidl_type_t bounded_32_nonnullable_vector_of_handles = {
    .type_tag = kFidlTypeVector,
    {.coded_vector = {.element = &nonnullable_handle,
                      .max_count = 32,
                      .element_size = sizeof(zx_handle_t),
                      .nullable = kFidlNullability_Nonnullable,
                      .alt_type = nullptr}}};
const fidl_type_t bounded_32_nullable_vector_of_handles = {
    .type_tag = kFidlTypeVector,
    {.coded_vector = {.element = &nonnullable_handle,
                      .max_count = 32,
                      .element_size = sizeof(zx_handle_t),
                      .nullable = kFidlNullability_Nullable,
                      .alt_type = nullptr}}};
const fidl_type_t bounded_2_nonnullable_vector_of_handles = {
    .type_tag = kFidlTypeVector,
    {.coded_vector = {.element = &nonnullable_handle,
                      .max_count = 2,
                      .element_size = sizeof(zx_handle_t),
                      .nullable = kFidlNullability_Nonnullable,
                      .alt_type = nullptr}}};
const fidl_type_t bounded_2_nullable_vector_of_handles = {
    .type_tag = kFidlTypeVector,
    {.coded_vector = {.element = &nonnullable_handle,
                      .max_count = 2,
                      .element_size = sizeof(zx_handle_t),
                      .nullable = kFidlNullability_Nullable,
                      .alt_type = nullptr}}};

const fidl_type_t unbounded_nonnullable_vector_of_uint32 = {
    .type_tag = kFidlTypeVector,
    {.coded_vector = {.element = nullptr,
                      .max_count = FIDL_MAX_SIZE,
                      .element_size = sizeof(uint32_t),
                      .nullable = kFidlNullability_Nonnullable,
                      .alt_type = nullptr}}};
const fidl_type_t unbounded_nullable_vector_of_uint32 = {
    .type_tag = kFidlTypeVector,
    {.coded_vector = {.element = nullptr,
                      .max_count = FIDL_MAX_SIZE,
                      .element_size = sizeof(uint32_t),
                      .nullable = kFidlNullability_Nullable,
                      .alt_type = nullptr}}};
const fidl_type_t bounded_32_nonnullable_vector_of_uint32{
    .type_tag = kFidlTypeVector,
    {.coded_vector = {.element = nullptr,
                      .max_count = 32,
                      .element_size = sizeof(uint32_t),
                      .nullable = kFidlNullability_Nonnullable,
                      .alt_type = nullptr}}};
const fidl_type_t bounded_32_nullable_vector_of_uint32{
    .type_tag = kFidlTypeVector,
    {.coded_vector = {.element = nullptr,
                      .max_count = 32,
                      .element_size = sizeof(uint32_t),
                      .nullable = kFidlNullability_Nullable,
                      .alt_type = nullptr}}};
const fidl_type_t bounded_2_nonnullable_vector_of_uint32{
    .type_tag = kFidlTypeVector,
    {.coded_vector = {.element = nullptr,
                      .max_count = 2,
                      .element_size = sizeof(uint32_t),
                      .nullable = kFidlNullability_Nonnullable,
                      .alt_type = nullptr}}};
const fidl_type_t bounded_2_nullable_vector_of_uint32{
    .type_tag = kFidlTypeVector,
    {.coded_vector = {.element = nullptr,
                      .max_count = 2,
                      .element_size = sizeof(uint32_t),
                      .nullable = kFidlNullability_Nullable,
                      .alt_type = nullptr}}};

// Handle messages.
static const FidlStructField nonnullable_handle_message_fields[] = {
    FidlStructField(&nonnullable_handle,
                    offsetof(nonnullable_handle_message_layout, inline_struct.handle), 4),
};
const fidl_type_t nonnullable_handle_message_type = {
    .type_tag = kFidlTypeStruct,
    {.coded_struct = {.fields = nonnullable_handle_message_fields,
                      .field_count = ArrayCount(nonnullable_handle_message_fields),
                      .size = sizeof(nonnullable_handle_inline_data),
                      .max_out_of_line = UINT32_MAX,
                      .contains_union = true,
                      .name = "nonnullable_handle_message",
                      .alt_type = nullptr}}};

static const FidlStructField multiple_nonnullable_handles_fields[] = {
    FidlStructField(&nonnullable_handle,
                    offsetof(multiple_nonnullable_handles_message_layout, inline_struct.handle_0),
                    0),
    FidlStructField(&nonnullable_channel_handle,
                    offsetof(multiple_nonnullable_handles_message_layout, inline_struct.handle_1),
                    0),
    FidlStructField(&nonnullable_vmo_handle,
                    offsetof(multiple_nonnullable_handles_message_layout, inline_struct.handle_2),
                    0),
};
const fidl_type_t multiple_nonnullable_handles_message_type = {
    .type_tag = kFidlTypeStruct,
    {.coded_struct = {.fields = multiple_nonnullable_handles_fields,
                      .field_count = ArrayCount(multiple_nonnullable_handles_fields),
                      .size = sizeof(multiple_nonnullable_handles_inline_data),
                      .max_out_of_line = UINT32_MAX,
                      .contains_union = true,
                      .name = "multiple_nonnullable_handles_message",
                      .alt_type = nullptr}}};

static const FidlStructField nullable_handle_fields[] = {
    FidlStructField(&nullable_handle,
                    offsetof(nullable_handle_message_layout, inline_struct.handle), 4),

};
const fidl_type_t nullable_handle_message_type = {
    .type_tag = kFidlTypeStruct,
    {.coded_struct = {.fields = nullable_handle_fields,
                      .field_count = ArrayCount(nullable_handle_fields),
                      .size = sizeof(nullable_handle_inline_data),
                      .max_out_of_line = UINT32_MAX,
                      .contains_union = true,
                      .name = "nullable_handle_message",
                      .alt_type = nullptr}}};

static const FidlStructField multiple_nullable_handles_fields[] = {
    FidlStructField(&nullable_handle,
                    offsetof(multiple_nullable_handles_message_layout, inline_struct.handle_0), 0),
    FidlStructField(&nullable_channel_handle,
                    offsetof(multiple_nullable_handles_message_layout, inline_struct.handle_1), 0),
    FidlStructField(&nullable_vmo_handle,
                    offsetof(multiple_nullable_handles_message_layout, inline_struct.handle_2), 0),
};
const fidl_type_t multiple_nullable_handles_message_type = {
    .type_tag = kFidlTypeStruct,
    {.coded_struct = {.fields = multiple_nullable_handles_fields,
                      .field_count = ArrayCount(multiple_nullable_handles_fields),
                      .size = sizeof(multiple_nullable_handles_inline_data),
                      .max_out_of_line = UINT32_MAX,
                      .contains_union = true,
                      .name = "multiple_nullable_handles_message",
                      .alt_type = nullptr}}};

// Array messages.
static const FidlStructField array_of_nonnullable_handles_fields[] = {
    FidlStructField(&array_of_four_nonnullable_handles,
                    offsetof(array_of_nonnullable_handles_message_layout, inline_struct.handles),
                    0),
};
const fidl_type_t array_of_nonnullable_handles_message_type = {
    .type_tag = kFidlTypeStruct,
    {.coded_struct = {.fields = array_of_nonnullable_handles_fields,
                      .field_count = ArrayCount(array_of_nonnullable_handles_fields),
                      .size = sizeof(array_of_nonnullable_handles_inline_data),
                      .max_out_of_line = UINT32_MAX,
                      .contains_union = true,
                      .name = "array_of_nonnullable_handles_message",
                      .alt_type = nullptr}}};

static const FidlStructField array_of_nullable_handles_fields[] = {
    FidlStructField(&array_of_five_nullable_handles,
                    offsetof(array_of_nullable_handles_message_layout, inline_struct.handles), 0),
};
const fidl_type_t array_of_nullable_handles_message_type = {
    .type_tag = kFidlTypeStruct,
    {.coded_struct = {.fields = array_of_nullable_handles_fields,
                      .field_count = ArrayCount(array_of_nullable_handles_fields),
                      .size = sizeof(array_of_nullable_handles_inline_data),
                      .max_out_of_line = UINT32_MAX,
                      .contains_union = true,
                      .name = "array_of_nullable_handles_message",
                      .alt_type = nullptr}}};

static const FidlStructField array_of_array_of_nonnullable_handles_fields[] = {
    FidlStructField(
        &array_of_three_arrays_of_four_nonnullable_handles,
        offsetof(array_of_array_of_nonnullable_handles_message_layout, inline_struct.handles), 0),
};
const fidl_type_t array_of_array_of_nonnullable_handles_message_type = {
    .type_tag = kFidlTypeStruct,
    {.coded_struct = {.fields = array_of_array_of_nonnullable_handles_fields,
                      .field_count = ArrayCount(array_of_array_of_nonnullable_handles_fields),
                      .size = sizeof(array_of_array_of_nonnullable_handles_inline_data),
                      .max_out_of_line = UINT32_MAX,
                      .contains_union = true,
                      .name = "array_of_array_of_nonnullable_handles_message",
                      .alt_type = nullptr}}};

static const FidlStructField out_of_line_fields[] = {
    FidlStructField(&array_of_four_nonnullable_handles,
                    offsetof(array_of_nonnullable_handles, handles), 0),

};
static const fidl_type_t out_of_line_type = {
    .type_tag = kFidlTypeStruct,
    {.coded_struct = {.fields = out_of_line_fields,
                      .field_count = ArrayCount(out_of_line_fields),
                      .size = sizeof(array_of_nonnullable_handles),
                      .max_out_of_line = UINT32_MAX,
                      .contains_union = true,
                      .name = "out_of_line",
                      .alt_type = nullptr}}};
static const fidl_type_t out_of_line_pointer_type = {
    .type_tag = kFidlTypeStructPointer,
    {.coded_struct_pointer = {.struct_type = &out_of_line_type.coded_struct}}};

static const FidlStructField out_of_line_array_of_nonnullable_handles_fields[] = {
    FidlStructField(&out_of_line_pointer_type,
                    offsetof(out_of_line_array_of_nonnullable_handles_message_layout,
                             inline_struct.maybe_array),
                    0),
};
const fidl_type_t out_of_line_array_of_nonnullable_handles_message_type = {
    .type_tag = kFidlTypeStruct,
    {.coded_struct = {.fields = out_of_line_array_of_nonnullable_handles_fields,
                      .field_count = ArrayCount(out_of_line_array_of_nonnullable_handles_fields),
                      .size = sizeof(out_of_line_array_of_nonnullable_handles_inline_data),
                      .max_out_of_line = UINT32_MAX,
                      .contains_union = true,
                      .name = "out_of_line_array_of_nonnullable_handles_message",
                      .alt_type = nullptr}}};

// String messages.
static const FidlStructField unbounded_nonnullable_string_fields[] = {
    FidlStructField(&unbounded_nonnullable_string,
                    offsetof(unbounded_nonnullable_string_message_layout, inline_struct.string), 0),

};
const fidl_type_t unbounded_nonnullable_string_message_type = {
    .type_tag = kFidlTypeStruct,
    {.coded_struct = {.fields = unbounded_nonnullable_string_fields,
                      .field_count = ArrayCount(unbounded_nonnullable_string_fields),
                      .size = sizeof(unbounded_nonnullable_string_inline_data),
                      .max_out_of_line = UINT32_MAX,
                      .contains_union = true,
                      .name = "unbounded_nonnullable_string_message",
                      .alt_type = nullptr}}};

static const FidlStructField unbounded_nullable_string_fields[] = {
    FidlStructField(&unbounded_nullable_string,
                    offsetof(unbounded_nullable_string_message_layout, inline_struct.string), 0),

};
const fidl_type_t unbounded_nullable_string_message_type = {
    .type_tag = kFidlTypeStruct,
    {.coded_struct = {.fields = unbounded_nullable_string_fields,
                      .field_count = ArrayCount(unbounded_nullable_string_fields),
                      .size = sizeof(unbounded_nullable_string_inline_data),
                      .max_out_of_line = UINT32_MAX,
                      .contains_union = true,
                      .name = "unbounded_nullable_string_message",
                      .alt_type = nullptr}}};

static const FidlStructField bounded_32_nonnullable_string_fields[] = {
    FidlStructField(&bounded_32_nonnullable_string,
                    offsetof(bounded_32_nonnullable_string_message_layout, inline_struct.string),
                    0),

};
const fidl_type_t bounded_32_nonnullable_string_message_type = {
    .type_tag = kFidlTypeStruct,
    {.coded_struct = {.fields = bounded_32_nonnullable_string_fields,
                      .field_count = ArrayCount(bounded_32_nonnullable_string_fields),
                      .size = sizeof(bounded_32_nonnullable_string_inline_data),
                      .max_out_of_line = UINT32_MAX,
                      .contains_union = true,
                      .name = "bounded_32_nonnullable_string_message",
                      .alt_type = nullptr}}};

static const FidlStructField bounded_32_nullable_string_fields[] = {
    FidlStructField(&bounded_32_nullable_string,
                    offsetof(bounded_32_nullable_string_message_layout, inline_struct.string), 0),
};
const fidl_type_t bounded_32_nullable_string_message_type = {
    .type_tag = kFidlTypeStruct,
    {.coded_struct = {.fields = bounded_32_nullable_string_fields,
                      .field_count = ArrayCount(bounded_32_nullable_string_fields),
                      .size = sizeof(bounded_32_nullable_string_inline_data),
                      .max_out_of_line = UINT32_MAX,
                      .contains_union = true,
                      .name = "bounded_32_nullable_string_message",
                      .alt_type = nullptr}}};

static const FidlStructField multiple_nonnullable_strings_fields[] = {
    FidlStructField(&bounded_32_nonnullable_string,
                    offsetof(multiple_nonnullable_strings_message_layout, inline_struct.string), 0),

    FidlStructField(&bounded_32_nonnullable_string,
                    offsetof(multiple_nonnullable_strings_message_layout, inline_struct.string2),
                    0),
};
const fidl_type_t multiple_nonnullable_strings_message_type = {
    .type_tag = kFidlTypeStruct,
    {.coded_struct = {.fields = multiple_nonnullable_strings_fields,
                      .field_count = ArrayCount(multiple_nonnullable_strings_fields),
                      .size = sizeof(multiple_nonnullable_strings_inline_data),
                      .max_out_of_line = UINT32_MAX,
                      .contains_union = true,
                      .name = "multiple_nonnullable_strings_message",
                      .alt_type = nullptr}}};

static const FidlStructField multiple_nullable_strings_fields[] = {
    FidlStructField(&bounded_32_nullable_string,
                    offsetof(multiple_nullable_strings_message_layout, inline_struct.string), 0),

    FidlStructField(&bounded_32_nullable_string,
                    offsetof(multiple_nullable_strings_message_layout, inline_struct.string2), 0),
};
const fidl_type_t multiple_nullable_strings_message_type = {
    .type_tag = kFidlTypeStruct,
    {.coded_struct = {.fields = multiple_nullable_strings_fields,
                      .field_count = ArrayCount(multiple_nullable_strings_fields),
                      .size = sizeof(multiple_nullable_strings_inline_data),
                      .max_out_of_line = UINT32_MAX,
                      .contains_union = true,
                      .name = "multiple_nullable_strings_message",
                      .alt_type = nullptr}}};

static const FidlStructField multiple_short_nonnullable_strings_fields[] = {
    FidlStructField(
        &bounded_4_nonnullable_string,
        offsetof(multiple_short_nonnullable_strings_message_layout, inline_struct.string), 0),

    FidlStructField(
        &bounded_32_nonnullable_string,
        offsetof(multiple_short_nonnullable_strings_message_layout, inline_struct.string2), 0),
};
const fidl_type_t multiple_short_nonnullable_strings_message_type = {
    .type_tag = kFidlTypeStruct,
    {.coded_struct = {.fields = multiple_short_nonnullable_strings_fields,
                      .field_count = ArrayCount(multiple_short_nonnullable_strings_fields),
                      .size = sizeof(multiple_short_nonnullable_strings_inline_data),
                      .max_out_of_line = UINT32_MAX,
                      .contains_union = true,
                      .name = "multiple_short_nonnullable_strings_message",
                      .alt_type = nullptr}}};

static const FidlStructField multiple_short_nullable_strings_fields[] = {
    FidlStructField(&bounded_4_nullable_string,
                    offsetof(multiple_short_nullable_strings_message_layout, inline_struct.string),
                    0),

    FidlStructField(&bounded_32_nullable_string,
                    offsetof(multiple_short_nullable_strings_message_layout, inline_struct.string2),
                    0),
};
const fidl_type_t multiple_short_nullable_strings_message_type = {
    .type_tag = kFidlTypeStruct,
    {.coded_struct = {.fields = multiple_short_nullable_strings_fields,
                      .field_count = ArrayCount(multiple_short_nullable_strings_fields),
                      .size = sizeof(multiple_short_nullable_strings_inline_data),
                      .max_out_of_line = UINT32_MAX,
                      .contains_union = true,
                      .name = "multiple_short_nullable_strings_message",
                      .alt_type = nullptr}}};

// Vector messages.
static const FidlStructField unbounded_nonnullable_vector_of_handles_fields[] = {
    FidlStructField(
        &unbounded_nonnullable_vector_of_handles,
        offsetof(unbounded_nonnullable_vector_of_handles_message_layout, inline_struct.vector), 0),

};
const fidl_type_t unbounded_nonnullable_vector_of_handles_message_type = {
    .type_tag = kFidlTypeStruct,
    {.coded_struct = {.fields = unbounded_nonnullable_vector_of_handles_fields,
                      .field_count = ArrayCount(unbounded_nonnullable_vector_of_handles_fields),
                      .size = sizeof(unbounded_nonnullable_vector_of_handles_inline_data),
                      .max_out_of_line = UINT32_MAX,
                      .contains_union = true,
                      .name = "unbounded_nonnullable_vector_of_handles_message",
                      .alt_type = nullptr}}};

static const FidlStructField unbounded_nullable_vector_of_handles_fields[] = {
    FidlStructField(
        &unbounded_nullable_vector_of_handles,
        offsetof(unbounded_nullable_vector_of_handles_message_layout, inline_struct.vector), 0),

};
const fidl_type_t unbounded_nullable_vector_of_handles_message_type = {
    .type_tag = kFidlTypeStruct,
    {.coded_struct = {.fields = unbounded_nullable_vector_of_handles_fields,
                      .field_count = ArrayCount(unbounded_nullable_vector_of_handles_fields),
                      .size = sizeof(unbounded_nullable_vector_of_handles_inline_data),
                      .max_out_of_line = UINT32_MAX,
                      .contains_union = true,
                      .name = "unbounded_nullable_vector_of_handles_message",
                      .alt_type = nullptr}}};

static const FidlStructField bounded_32_nonnullable_vector_of_handles_fields[] = {
    FidlStructField(
        &bounded_32_nonnullable_vector_of_handles,
        offsetof(bounded_32_nonnullable_vector_of_handles_message_layout, inline_struct.vector), 0),

};
const fidl_type_t bounded_32_nonnullable_vector_of_handles_message_type = {
    .type_tag = kFidlTypeStruct,
    {.coded_struct = {.fields = bounded_32_nonnullable_vector_of_handles_fields,
                      .field_count = ArrayCount(bounded_32_nonnullable_vector_of_handles_fields),
                      .size = sizeof(bounded_32_nonnullable_vector_of_handles_inline_data),
                      .max_out_of_line = UINT32_MAX,
                      .contains_union = true,
                      .name = "bounded_32_nonnullable_vector_of_handles_message",
                      .alt_type = nullptr}}};

static const FidlStructField bounded_32_nullable_vector_of_handles_fields[] = {
    FidlStructField(
        &bounded_32_nullable_vector_of_handles,
        offsetof(bounded_32_nullable_vector_of_handles_message_layout, inline_struct.vector), 0),

};
const fidl_type_t bounded_32_nullable_vector_of_handles_message_type = {
    .type_tag = kFidlTypeStruct,
    {.coded_struct{.fields = bounded_32_nullable_vector_of_handles_fields,
                   .field_count = ArrayCount(bounded_32_nullable_vector_of_handles_fields),
                   .size = sizeof(bounded_32_nullable_vector_of_handles_inline_data),
                   .max_out_of_line = UINT32_MAX,
                   .contains_union = true,
                   .name = "bounded_32_nullable_vector_of_handles_message",
                   .alt_type = nullptr}}};

static const FidlStructField multiple_nonnullable_vectors_of_handles_fields[] = {
    FidlStructField(
        &bounded_2_nonnullable_vector_of_handles,
        offsetof(multiple_nonnullable_vectors_of_handles_message_layout, inline_struct.vector), 0),
    FidlStructField(
        &unbounded_nonnullable_vector_of_handles,
        offsetof(multiple_nonnullable_vectors_of_handles_message_layout, inline_struct.vector2), 0),
};
const fidl_type_t multiple_nonnullable_vectors_of_handles_message_type = {
    .type_tag = kFidlTypeStruct,
    {.coded_struct = {.fields = multiple_nonnullable_vectors_of_handles_fields,
                      .field_count = ArrayCount(multiple_nonnullable_vectors_of_handles_fields),
                      .size = sizeof(multiple_nonnullable_vectors_of_handles_inline_data),
                      .max_out_of_line = UINT32_MAX,
                      .contains_union = true,
                      .name = "multiple_nonnullable_vectors_of_handles_message",
                      .alt_type = nullptr}}};

static const FidlStructField multiple_nullable_vectors_of_handles_fields[] = {
    FidlStructField(
        &bounded_2_nullable_vector_of_handles,
        offsetof(multiple_nullable_vectors_of_handles_message_layout, inline_struct.vector), 0),
    FidlStructField(
        &unbounded_nullable_vector_of_handles,
        offsetof(multiple_nullable_vectors_of_handles_message_layout, inline_struct.vector2), 0),
};
const fidl_type_t multiple_nullable_vectors_of_handles_message_type = {
    .type_tag = kFidlTypeStruct,
    {.coded_struct = {.fields = multiple_nullable_vectors_of_handles_fields,
                      .field_count = ArrayCount(multiple_nullable_vectors_of_handles_fields),
                      .size = sizeof(multiple_nullable_vectors_of_handles_inline_data),
                      .max_out_of_line = UINT32_MAX,
                      .contains_union = true,
                      .name = "multiple_nullable_vectors_of_handles_message",
                      .alt_type = nullptr}}};

static const FidlStructField unbounded_nonnullable_vector_of_uint32_fields[] = {
    FidlStructField(
        &unbounded_nonnullable_vector_of_uint32,
        offsetof(unbounded_nonnullable_vector_of_uint32_message_layout, inline_struct.vector), 0),

};
const fidl_type_t unbounded_nonnullable_vector_of_uint32_message_type = {
    .type_tag = kFidlTypeStruct,
    {.coded_struct = {.fields = unbounded_nonnullable_vector_of_uint32_fields,
                      .field_count = ArrayCount(unbounded_nonnullable_vector_of_uint32_fields),
                      .size = sizeof(unbounded_nonnullable_vector_of_uint32_inline_data),
                      .max_out_of_line = UINT32_MAX,
                      .contains_union = true,
                      .name = "unbounded_nonnullable_vector_of_uint32_message",
                      .alt_type = nullptr}}};

static const FidlStructField unbounded_nullable_vector_of_uint32_fields[] = {
    FidlStructField(
        &unbounded_nullable_vector_of_uint32,
        offsetof(unbounded_nullable_vector_of_uint32_message_layout, inline_struct.vector), 0),

};
const fidl_type_t unbounded_nullable_vector_of_uint32_message_type = {
    .type_tag = kFidlTypeStruct,
    {.coded_struct = {.fields = unbounded_nullable_vector_of_uint32_fields,
                      .field_count = ArrayCount(unbounded_nullable_vector_of_uint32_fields),
                      .size = sizeof(unbounded_nullable_vector_of_uint32_inline_data),
                      .max_out_of_line = UINT32_MAX,
                      .contains_union = true,
                      .name = "unbounded_nullable_vector_of_uint32_message",
                      .alt_type = nullptr}}};

static const FidlStructField bounded_32_nonnullable_vector_of_uint32_fields[] = {
    FidlStructField(
        &bounded_32_nonnullable_vector_of_uint32,
        offsetof(bounded_32_nonnullable_vector_of_uint32_message_layout, inline_struct.vector), 0),

};
const fidl_type_t bounded_32_nonnullable_vector_of_uint32_message_type = {
    .type_tag = kFidlTypeStruct,
    {.coded_struct = {.fields = bounded_32_nonnullable_vector_of_uint32_fields,
                      .field_count = ArrayCount(bounded_32_nonnullable_vector_of_uint32_fields),
                      .size = sizeof(bounded_32_nonnullable_vector_of_uint32_inline_data),
                      .max_out_of_line = UINT32_MAX,
                      .contains_union = true,
                      .name = "bounded_32_nonnullable_vector_of_uint32_message",
                      .alt_type = nullptr}}};

static const FidlStructField bounded_32_nullable_vector_of_uint32_fields[] = {
    FidlStructField(
        &bounded_32_nullable_vector_of_uint32,
        offsetof(bounded_32_nullable_vector_of_uint32_message_layout, inline_struct.vector), 0),

};
const fidl_type_t bounded_32_nullable_vector_of_uint32_message_type = {
    .type_tag = kFidlTypeStruct,
    {.coded_struct = {.fields = bounded_32_nullable_vector_of_uint32_fields,
                      .field_count = ArrayCount(bounded_32_nullable_vector_of_uint32_fields),
                      .size = sizeof(bounded_32_nullable_vector_of_uint32_inline_data),
                      .max_out_of_line = UINT32_MAX,
                      .contains_union = true,
                      .name = "bounded_32_nullable_vector_of_uint32_message",
                      .alt_type = nullptr}}};

static const FidlStructField multiple_nonnullable_vectors_of_uint32_fields[] = {
    FidlStructField(
        &bounded_2_nonnullable_vector_of_uint32,
        offsetof(multiple_nonnullable_vectors_of_uint32_message_layout, inline_struct.vector), 0),
    FidlStructField(
        &unbounded_nonnullable_vector_of_uint32,
        offsetof(multiple_nonnullable_vectors_of_uint32_message_layout, inline_struct.vector2), 0),
};
const fidl_type_t multiple_nonnullable_vectors_of_uint32_message_type = {
    .type_tag = kFidlTypeStruct,
    {.coded_struct = {.fields = multiple_nonnullable_vectors_of_uint32_fields,
                      .field_count = ArrayCount(multiple_nonnullable_vectors_of_uint32_fields),
                      .size = sizeof(multiple_nonnullable_vectors_of_uint32_inline_data),
                      .max_out_of_line = UINT32_MAX,
                      .contains_union = true,
                      .name = "multiple_nonnullable_vectors_of_uint32_message",
                      .alt_type = nullptr}}};

static const FidlStructField multiple_nullable_vectors_of_uint32_fields[] = {
    FidlStructField(
        &bounded_2_nullable_vector_of_uint32,
        offsetof(multiple_nullable_vectors_of_uint32_message_layout, inline_struct.vector), 0),
    FidlStructField(
        &unbounded_nullable_vector_of_uint32,
        offsetof(multiple_nullable_vectors_of_uint32_message_layout, inline_struct.vector2), 0),
};
const fidl_type_t multiple_nullable_vectors_of_uint32_message_type = {
    .type_tag = kFidlTypeStruct,
    {.coded_struct = {.fields = multiple_nullable_vectors_of_uint32_fields,
                      .field_count = ArrayCount(multiple_nullable_vectors_of_uint32_fields),
                      .size = sizeof(multiple_nullable_vectors_of_uint32_inline_data),
                      .max_out_of_line = UINT32_MAX,
                      .contains_union = true,
                      .name = "multiple_nullable_vectors_of_uint32_message",
                      .alt_type = nullptr}}};

// Union messages.
static const FidlUnionField nonnullable_handle_union_members[] = {
    {&nonnullable_handle, 0, 0},
};
const fidl_type_t nonnullable_handle_union_type = {
    .type_tag = kFidlTypeUnion,
    {.coded_union = {.fields = nonnullable_handle_union_members,
                     .field_count = ArrayCount(nonnullable_handle_union_members),
                     .data_offset = offsetof(nonnullable_handle_union, handle),
                     .size = sizeof(nonnullable_handle_union),
                     .name = "nonnullable_handle_union",
                     .alt_type = nullptr}}};
static const FidlStructField nonnullable_handle_union_fields[] = {
    FidlStructField(&nonnullable_handle_union_type,
                    offsetof(nonnullable_handle_union_message_layout, inline_struct.data), 0),
};
const fidl_type_t nonnullable_handle_union_message_type = {
    .type_tag = kFidlTypeStruct,
    {.coded_struct = {.fields = nonnullable_handle_union_fields,
                      .field_count = ArrayCount(nonnullable_handle_union_fields),
                      .size = sizeof(nonnullable_handle_union_inline_data),
                      .max_out_of_line = UINT32_MAX,
                      .contains_union = true,
                      .name = "nonnullable_handle_union_message",
                      .alt_type = nullptr}}};

static const FidlUnionField array_of_nonnullable_handles_union_members[] = {
    {&nonnullable_handle, 16, 0},
    {&array_of_two_nonnullable_handles, 12, 0},
    {&array_of_two_arrays_of_two_nonnullable_handles, 4, 0},
};
static const fidl_type_t array_of_nonnullable_handles_union_type = {
    .type_tag = kFidlTypeUnion,
    {.coded_union = {.fields = array_of_nonnullable_handles_union_members,
                     .field_count = ArrayCount(array_of_nonnullable_handles_union_members),
                     .data_offset = offsetof(array_of_nonnullable_handles_union, handle),
                     .size = sizeof(array_of_nonnullable_handles_union),
                     .name = "array_of_nonnullable_handles_union",
                     .alt_type = nullptr}}};
static const FidlStructField array_of_nonnullable_handles_union_fields[] = {
    FidlStructField(&array_of_nonnullable_handles_union_type,
                    offsetof(array_of_nonnullable_handles_union_message_layout, inline_struct.data),
                    0),

};
const fidl_type_t array_of_nonnullable_handles_union_message_type = {
    .type_tag = kFidlTypeStruct,
    {.coded_struct = {.fields = array_of_nonnullable_handles_union_fields,
                      .field_count = ArrayCount(array_of_nonnullable_handles_union_fields),
                      .size = sizeof(array_of_nonnullable_handles_union_inline_data),
                      .max_out_of_line = UINT32_MAX,
                      .contains_union = true,
                      .name = "array_of_nonnullable_handles_union_message",
                      .alt_type = nullptr}}};

// Union pointer messages.
const fidl_type_t nonnullable_handle_union_ptr = {
    .type_tag = kFidlTypeUnionPointer,
    {.coded_union_pointer = {.union_type = &nonnullable_handle_union_type.coded_union}}};
static const FidlStructField nonnullable_handle_union_ptr_fields[] = {
    FidlStructField(&nonnullable_handle_union_ptr,
                    offsetof(nonnullable_handle_union_ptr_inline_data, data), 0),
};
const fidl_type_t nonnullable_handle_union_ptr_message_type = {
    .type_tag = kFidlTypeStruct,
    {.coded_struct = {.fields = nonnullable_handle_union_ptr_fields,
                      .field_count = ArrayCount(nonnullable_handle_union_ptr_fields),
                      .size = sizeof(nonnullable_handle_union_ptr_inline_data),
                      .max_out_of_line = UINT32_MAX,
                      .contains_union = true,
                      .name = "nonnullable_handle_union_ptr_message",
                      .alt_type = nullptr}}};

static const fidl_type_t array_of_nonnullable_handles_union_ptr = {
    .type_tag = kFidlTypeUnionPointer,
    {.coded_union_pointer = {.union_type = &array_of_nonnullable_handles_union_type.coded_union}}};
static const FidlStructField array_of_nonnullable_handles_union_ptr_fields[] = {
    FidlStructField(&array_of_nonnullable_handles_union_ptr,
                    offsetof(array_of_nonnullable_handles_union_ptr_inline_data, data), 0),
};
const fidl_type_t array_of_nonnullable_handles_union_ptr_message_type = {
    .type_tag = kFidlTypeStruct,
    {.coded_struct = {.fields = array_of_nonnullable_handles_union_ptr_fields,
                      .field_count = ArrayCount(array_of_nonnullable_handles_union_ptr_fields),
                      .size = sizeof(array_of_nonnullable_handles_union_ptr_inline_data),
                      .max_out_of_line = UINT32_MAX,
                      .contains_union = true,
                      .name = "array_of_nonnullable_handles_union_ptr_message",
                      .alt_type = nullptr}}};

// Struct messages.
static const FidlStructField struct_level_3_fields[] = {
    FidlStructField(&nonnullable_handle, offsetof(struct_level_3, handle_3), 0),
};
static const fidl_type_t struct_level_3_struct = {
    .type_tag = kFidlTypeStruct,
    {.coded_struct = {.fields = struct_level_3_fields,
                      .field_count = ArrayCount(struct_level_3_fields),
                      .size = sizeof(struct_level_3),
                      .max_out_of_line = UINT32_MAX,
                      .contains_union = true,
                      .name = "struct_level_3",
                      .alt_type = nullptr}}};
static const FidlStructField struct_level_2_fields[] = {
    FidlStructField(&struct_level_3_struct, offsetof(struct_level_2, l3), 0),
    FidlStructField(&nonnullable_handle, offsetof(struct_level_2, handle_2), 4),
};
static const fidl_type_t struct_level_2_struct = {
    .type_tag = kFidlTypeStruct,
    {.coded_struct = {.fields = struct_level_2_fields,
                      .field_count = ArrayCount(struct_level_2_fields),
                      .size = sizeof(struct_level_2),
                      .max_out_of_line = UINT32_MAX,
                      .contains_union = true,
                      .name = "struct_level_2",
                      .alt_type = nullptr}}};
static const FidlStructField struct_level_1_fields[] = {
    FidlStructField(&nonnullable_handle, offsetof(struct_level_1, handle_1), 4),
    FidlStructField(&struct_level_2_struct, offsetof(struct_level_1, l2), 0),
};
static const fidl_type_t struct_level_1_struct = {
    .type_tag = kFidlTypeStruct,
    {.coded_struct = {.fields = struct_level_1_fields,
                      .field_count = ArrayCount(struct_level_1_fields),
                      .size = sizeof(struct_level_1),
                      .max_out_of_line = UINT32_MAX,
                      .contains_union = true,
                      .name = "struct_level_1",
                      .alt_type = nullptr}}};
static const FidlStructField struct_level_0_fields[] = {
    FidlStructField(&struct_level_1_struct, offsetof(struct_level_0, l1), 0),
    FidlStructField(&nonnullable_handle, offsetof(struct_level_0, handle_0), 4),
};
const fidl_type_t struct_level_0_struct = {
    .type_tag = kFidlTypeStruct,
    {.coded_struct = {.fields = struct_level_0_fields,
                      .field_count = ArrayCount(struct_level_1_fields),
                      .size = sizeof(struct_level_0),
                      .max_out_of_line = UINT32_MAX,
                      .contains_union = true,
                      .name = "struct_level_0",
                      .alt_type = nullptr}}};
static const FidlStructField nested_structs_fields[] = {
    FidlStructField(&struct_level_0_struct,
                    offsetof(nested_structs_message_layout, inline_struct.l0), 0),

};
const fidl_type_t nested_structs_message_type = {
    .type_tag = kFidlTypeStruct,
    {.coded_struct = {.fields = nested_structs_fields,
                      .field_count = ArrayCount(nested_structs_fields),
                      .size = sizeof(nested_structs_inline_data),
                      .max_out_of_line = UINT32_MAX,
                      .contains_union = true,
                      .name = "nested_structs_message",
                      .alt_type = nullptr}}};

// Struct pointer messages.
static const FidlStructField struct_ptr_level_3_fields[] = {
    FidlStructField(&nonnullable_handle, offsetof(struct_ptr_level_3, handle_3), 0),
};
static const fidl_type_t struct_ptr_level_3_struct = {
    .type_tag = kFidlTypeStruct,
    {.coded_struct = {.fields = struct_ptr_level_3_fields,
                      .field_count = ArrayCount(struct_ptr_level_3_fields),
                      .size = sizeof(struct_ptr_level_3),
                      .max_out_of_line = UINT32_MAX,
                      .contains_union = true,
                      .name = "struct_ptr_level_3",
                      .alt_type = nullptr}}};
static const fidl_type_t struct_ptr_level_3_struct_pointer = {
    .type_tag = kFidlTypeStructPointer,
    {.coded_struct_pointer = {.struct_type = &struct_ptr_level_3_struct.coded_struct}}};
static const FidlStructField struct_ptr_level_2_fields[] = {
    FidlStructField(&struct_ptr_level_3_struct_pointer, offsetof(struct_ptr_level_2, l3_present),
                    0),
    FidlStructField(&struct_ptr_level_3_struct_pointer, offsetof(struct_ptr_level_2, l3_absent), 0),
    FidlStructField(&struct_ptr_level_3_struct, offsetof(struct_ptr_level_2, l3_inline), 0),
    FidlStructField(&nonnullable_handle, offsetof(struct_ptr_level_2, handle_2), 4),
};
static const fidl_type_t struct_ptr_level_2_struct = {
    .type_tag = kFidlTypeStruct,
    {.coded_struct = {.fields = struct_ptr_level_2_fields,
                      .field_count = ArrayCount(struct_ptr_level_2_fields),
                      .size = sizeof(struct_ptr_level_2),
                      .max_out_of_line = UINT32_MAX,
                      .contains_union = true,
                      .name = "struct_ptr_level_2",
                      .alt_type = nullptr}}};
static const fidl_type_t struct_ptr_level_2_struct_pointer = {
    .type_tag = kFidlTypeStructPointer,
    {.coded_struct_pointer = {.struct_type = &struct_ptr_level_2_struct.coded_struct}}};
static const FidlStructField struct_ptr_level_1_fields[] = {
    FidlStructField(&nonnullable_handle, offsetof(struct_ptr_level_1, handle_1), 4),
    FidlStructField(&struct_ptr_level_2_struct_pointer, offsetof(struct_ptr_level_1, l2_present),
                    0),
    FidlStructField(&struct_ptr_level_2_struct, offsetof(struct_ptr_level_1, l2_inline), 0),
    FidlStructField(&struct_ptr_level_2_struct_pointer, offsetof(struct_ptr_level_1, l2_absent), 0),
};
static const fidl_type_t struct_ptr_level_1_struct = {
    .type_tag = kFidlTypeStruct,
    {.coded_struct = {.fields = struct_ptr_level_1_fields,
                      .field_count = ArrayCount(struct_ptr_level_1_fields),
                      .size = sizeof(struct_ptr_level_1),
                      .max_out_of_line = UINT32_MAX,
                      .contains_union = true,
                      .name = "struct_ptr_level_1",
                      .alt_type = nullptr}}};
static const fidl_type_t struct_ptr_level_1_struct_pointer = {
    .type_tag = kFidlTypeStructPointer,
    {.coded_struct_pointer = {.struct_type = &struct_ptr_level_1_struct.coded_struct}}};
static const FidlStructField struct_ptr_level_0_fields[] = {
    FidlStructField(&struct_ptr_level_1_struct_pointer, offsetof(struct_ptr_level_0, l1_absent), 0),
    FidlStructField(&struct_ptr_level_1_struct, offsetof(struct_ptr_level_0, l1_inline), 0),
    FidlStructField(&nonnullable_handle, offsetof(struct_ptr_level_0, handle_0), 4),
    FidlStructField(&struct_ptr_level_1_struct_pointer, offsetof(struct_ptr_level_0, l1_present),
                    0),
};
static const fidl_type_t struct_ptr_level_0_struct = {
    .type_tag = kFidlTypeStruct,
    {.coded_struct = {.fields = struct_ptr_level_0_fields,
                      .field_count = ArrayCount(struct_ptr_level_0_fields),
                      .size = sizeof(struct_ptr_level_0),
                      .max_out_of_line = UINT32_MAX,
                      .contains_union = true,
                      .name = "struct_ptr_level_0",
                      .alt_type = nullptr}}};
const fidl_type_t struct_ptr_level_0_struct_pointer = {
    .type_tag = kFidlTypeStructPointer,
    {.coded_struct_pointer = {.struct_type = &struct_ptr_level_0_struct.coded_struct}}};
static const FidlStructField nested_struct_ptrs_fields[] = {
    FidlStructField(&struct_ptr_level_0_struct, offsetof(nested_struct_ptrs_inline_data, l0_inline),
                    0),
    FidlStructField(&struct_ptr_level_0_struct_pointer,
                    offsetof(nested_struct_ptrs_inline_data, l0_absent), 0),
    FidlStructField(&struct_ptr_level_0_struct_pointer,
                    offsetof(nested_struct_ptrs_inline_data, l0_present), 0),
};
const fidl_type_t nested_struct_ptrs_message_type = {
    .type_tag = kFidlTypeStruct,
    {.coded_struct = {.fields = nested_struct_ptrs_fields,
                      .field_count = ArrayCount(nested_struct_ptrs_fields),
                      .size = sizeof(nested_struct_ptrs_inline_data),
                      .max_out_of_line = UINT32_MAX,
                      .contains_union = true,
                      .name = "nested_struct_ptrs_message",
                      .alt_type = nullptr}}};

// Recursive struct pointer messages.
const fidl_type_t recursion_message_ptr_type = {
    .type_tag = kFidlTypeStructPointer,
    {.coded_struct_pointer = {.struct_type = &recursion_message_type.coded_struct}}};
static const FidlUnionField maybe_recurse_union_members[] = {
    {&nonnullable_handle, 4, 0},
    {&recursion_message_ptr_type, 0, 0},
};
const fidl_type_t maybe_recurse_type = {
    .type_tag = kFidlTypeUnion,
    {.coded_union = {.fields = maybe_recurse_union_members,
                     .field_count = ArrayCount(maybe_recurse_union_members),
                     .data_offset = offsetof(maybe_recurse, handle),
                     .size = sizeof(maybe_recurse),
                     .name = "maybe_recurse",
                     .alt_type = nullptr}}};
static const FidlStructField recursion_fields[] = {
    FidlStructField(&maybe_recurse_type, offsetof(recursion_inline_data, inline_union), 0),
};
const fidl_type_t recursion_message_type = {
    .type_tag = kFidlTypeStruct,
    {.coded_struct = {.fields = recursion_fields,
                      .field_count = ArrayCount(recursion_fields),
                      .size = sizeof(recursion_inline_data),
                      .max_out_of_line = UINT32_MAX,
                      .contains_union = true,
                      .name = "recursion_message",
                      .alt_type = nullptr}}};
