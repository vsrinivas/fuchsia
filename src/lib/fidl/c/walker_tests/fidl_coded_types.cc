// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl_coded_types.h"

#include <lib/fidl/internal.h>

#include "array_util.h"
#include "fidl_structs.h"

// Handle types.
const FidlCodedHandle nonnullable_handle = {
    .tag = kFidlTypeHandle,
    .nullable = kFidlNullability_Nonnullable,
    .handle_subtype = ZX_OBJ_TYPE_NONE,
    .handle_rights = ZX_RIGHT_SAME_RIGHTS,
};
const FidlCodedHandle nullable_handle = {
    .tag = kFidlTypeHandle,
    .nullable = kFidlNullability_Nullable,
    .handle_subtype = ZX_OBJ_TYPE_NONE,
    .handle_rights = 0,
};
const FidlCodedHandle nullable_channel_handle = {
    .tag = kFidlTypeHandle,
    .nullable = kFidlNullability_Nullable,
    .handle_subtype = ZX_OBJ_TYPE_CHANNEL,
    .handle_rights = ZX_RIGHT_READ | ZX_RIGHT_WRITE,
};
const FidlCodedHandle nullable_vmo_handle = {
    .tag = kFidlTypeHandle,
    .nullable = kFidlNullability_Nullable,
    .handle_subtype = ZX_OBJ_TYPE_VMO,
    .handle_rights = 0,
};
const FidlCodedHandle nonnullable_channel_handle = {
    .tag = kFidlTypeHandle,
    .nullable = kFidlNullability_Nonnullable,
    .handle_subtype = ZX_OBJ_TYPE_CHANNEL,
    .handle_rights = ZX_RIGHT_READ | ZX_RIGHT_WRITE,
};
const FidlCodedHandle nonnullable_vmo_handle = {
    .tag = kFidlTypeHandle,
    .nullable = kFidlNullability_Nonnullable,
    .handle_subtype = ZX_OBJ_TYPE_VMO,
    .handle_rights = 0,
};

// Array types.
const FidlCodedArray array_of_two_nonnullable_handles = {
    .tag = kFidlTypeArray,
    .element_size_v1 = sizeof(zx_handle_t),
    .element_size_v2 = sizeof(zx_handle_t),
    .array_size_v1 = 2 * sizeof(zx_handle_t),
    .array_size_v2 = 2 * sizeof(zx_handle_t),
    .element = &nonnullable_handle,
};
const FidlCodedArray array_of_four_nonnullable_handles = {
    .tag = kFidlTypeArray,
    .element_size_v1 = sizeof(zx_handle_t),
    .element_size_v2 = sizeof(zx_handle_t),
    .array_size_v1 = 4 * sizeof(zx_handle_t),
    .array_size_v2 = 4 * sizeof(zx_handle_t),
    .element = &nonnullable_handle,
};
const FidlCodedArray array_of_five_nullable_handles = {
    .tag = kFidlTypeArray,
    .element_size_v1 = sizeof(zx_handle_t),
    .element_size_v2 = sizeof(zx_handle_t),
    .array_size_v1 = 5 * sizeof(zx_handle_t),
    .array_size_v2 = 5 * sizeof(zx_handle_t),
    .element = &nullable_handle,
};
const FidlCodedArray array_of_three_arrays_of_four_nonnullable_handles = {
    .tag = kFidlTypeArray,
    .element_size_v1 = 4 * sizeof(zx_handle_t),
    .element_size_v2 = 4 * sizeof(zx_handle_t),
    .array_size_v1 = 3 * 4 * sizeof(zx_handle_t),
    .array_size_v2 = 3 * 4 * sizeof(zx_handle_t),
    .element = &array_of_four_nonnullable_handles,
};
const FidlCodedArray array_of_two_arrays_of_two_nonnullable_handles = {
    .tag = kFidlTypeArray,
    .element_size_v1 = 2 * sizeof(zx_handle_t),
    .element_size_v2 = 2 * sizeof(zx_handle_t),
    .array_size_v1 = 2 * 2 * sizeof(zx_handle_t),
    .array_size_v2 = 2 * 2 * sizeof(zx_handle_t),
    .element = &array_of_two_nonnullable_handles,
};

// String types.
const FidlCodedString unbounded_nonnullable_string = {
    .tag = kFidlTypeString,
    .nullable = kFidlNullability_Nonnullable,
    .max_size = FIDL_MAX_SIZE,
};
const FidlCodedString unbounded_nullable_string = {
    .tag = kFidlTypeString,
    .nullable = kFidlNullability_Nullable,
    .max_size = FIDL_MAX_SIZE,
};
const FidlCodedString bounded_32_nonnullable_string = {
    .tag = kFidlTypeString,
    .nullable = kFidlNullability_Nonnullable,
    .max_size = 32,
};
const FidlCodedString bounded_32_nullable_string = {
    .tag = kFidlTypeString,
    .nullable = kFidlNullability_Nullable,
    .max_size = 32,
};
const FidlCodedString bounded_4_nonnullable_string = {
    .tag = kFidlTypeString,
    .nullable = kFidlNullability_Nonnullable,
    .max_size = 4,
};
const FidlCodedString bounded_4_nullable_string = {
    .tag = kFidlTypeString,
    .nullable = kFidlNullability_Nullable,
    .max_size = 4,
};

// Vector types.
const FidlCodedVector unbounded_nonnullable_vector_of_handles = {
    .tag = kFidlTypeVector,
    .nullable = kFidlNullability_Nonnullable,
    .max_count = FIDL_MAX_SIZE,
    .element_size_v1 = sizeof(zx_handle_t),
    .element_size_v2 = sizeof(zx_handle_t),
    .element = &nonnullable_handle,
};
const FidlCodedVector unbounded_nullable_vector_of_handles = {
    .tag = kFidlTypeVector,
    .nullable = kFidlNullability_Nullable,
    .max_count = FIDL_MAX_SIZE,
    .element_size_v1 = sizeof(zx_handle_t),
    .element_size_v2 = sizeof(zx_handle_t),
    .element = &nonnullable_handle,
};
const FidlCodedVector bounded_32_nonnullable_vector_of_handles = {
    .tag = kFidlTypeVector,
    .nullable = kFidlNullability_Nonnullable,
    .max_count = 32,
    .element_size_v1 = sizeof(zx_handle_t),
    .element_size_v2 = sizeof(zx_handle_t),
    .element = &nonnullable_handle,
};
const FidlCodedVector bounded_32_nullable_vector_of_handles = {
    .tag = kFidlTypeVector,
    .nullable = kFidlNullability_Nullable,
    .max_count = 32,
    .element_size_v1 = sizeof(zx_handle_t),
    .element_size_v2 = sizeof(zx_handle_t),
    .element = &nonnullable_handle,
};
const FidlCodedVector bounded_2_nonnullable_vector_of_handles = {
    .tag = kFidlTypeVector,
    .nullable = kFidlNullability_Nonnullable,
    .max_count = 2,
    .element_size_v1 = sizeof(zx_handle_t),
    .element_size_v2 = sizeof(zx_handle_t),
    .element = &nonnullable_handle,
};
const FidlCodedVector bounded_2_nullable_vector_of_handles = {
    .tag = kFidlTypeVector,
    .nullable = kFidlNullability_Nullable,
    .max_count = 2,
    .element_size_v1 = sizeof(zx_handle_t),
    .element_size_v2 = sizeof(zx_handle_t),
    .element = &nonnullable_handle,
};

const FidlCodedVector unbounded_nonnullable_vector_of_uint32 = {
    .tag = kFidlTypeVector,
    .nullable = kFidlNullability_Nonnullable,
    .max_count = FIDL_MAX_SIZE,
    .element_size_v1 = sizeof(uint32_t),
    .element_size_v2 = sizeof(uint32_t),
    .element = nullptr,
};
const FidlCodedVector unbounded_nullable_vector_of_uint32 = {
    .tag = kFidlTypeVector,
    .nullable = kFidlNullability_Nullable,
    .max_count = FIDL_MAX_SIZE,
    .element_size_v1 = sizeof(uint32_t),
    .element_size_v2 = sizeof(uint32_t),
    .element = nullptr,
};
const FidlCodedVector bounded_32_nonnullable_vector_of_uint32 = {
    .tag = kFidlTypeVector,
    .nullable = kFidlNullability_Nonnullable,
    .max_count = 32,
    .element_size_v1 = sizeof(uint32_t),
    .element_size_v2 = sizeof(uint32_t),
    .element = nullptr,
};
const FidlCodedVector bounded_32_nullable_vector_of_uint32 = {
    .tag = kFidlTypeVector,
    .nullable = kFidlNullability_Nullable,
    .max_count = 32,
    .element_size_v1 = sizeof(uint32_t),
    .element_size_v2 = sizeof(uint32_t),
    .element = nullptr,
};
const FidlCodedVector bounded_2_nonnullable_vector_of_uint32 = {
    .tag = kFidlTypeVector,
    .nullable = kFidlNullability_Nonnullable,
    .max_count = 2,
    .element_size_v1 = sizeof(uint32_t),
    .element_size_v2 = sizeof(uint32_t),
    .element = nullptr,
};
const FidlCodedVector bounded_2_nullable_vector_of_uint32 = {
    .tag = kFidlTypeVector,
    .nullable = kFidlNullability_Nullable,
    .max_count = 2,
    .element_size_v1 = sizeof(uint32_t),
    .element_size_v2 = sizeof(uint32_t),
    .element = nullptr,
};

// Handle messages.
static const FidlStructElement nonnullable_handle_message_elements[] = {
    FidlStructElement::Field(&nonnullable_handle,
                             offsetof(nonnullable_handle_message_layout, inline_struct.handle) -
                                 sizeof(fidl_message_header_t),
                             offsetof(nonnullable_handle_message_layout, inline_struct.handle) -
                                 sizeof(fidl_message_header_t),
                             kFidlIsResource_Resource),
    FidlStructElement::Padding32(offsetof(nonnullable_handle_message_layout, inline_struct.handle) -
                                     sizeof(fidl_message_header_t) + 4,
                                 offsetof(nonnullable_handle_message_layout, inline_struct.handle) -
                                     sizeof(fidl_message_header_t) + 4,
                                 0xffffffff),
};
const FidlCodedStruct nonnullable_handle_message_type = {
    .tag = kFidlTypeStruct,
    .element_count = ArrayCount(nonnullable_handle_message_elements),
    .size_v1 = sizeof(nonnullable_handle_inline_data) - sizeof(fidl_message_header_t),
    .size_v2 = sizeof(nonnullable_handle_inline_data) - sizeof(fidl_message_header_t),
    .elements = nonnullable_handle_message_elements,
    .name = "nonnullable_handle_message",
};

static const FidlStructElement nonnullable_channel_message_elements[] = {
    FidlStructElement::Field(&nonnullable_channel_handle,
                             offsetof(nonnullable_handle_message_layout, inline_struct.handle) -
                                 sizeof(fidl_message_header_t),
                             offsetof(nonnullable_handle_message_layout, inline_struct.handle) -
                                 sizeof(fidl_message_header_t),
                             kFidlIsResource_Resource),
    FidlStructElement::Padding32(offsetof(nonnullable_handle_message_layout, inline_struct.handle) -
                                     sizeof(fidl_message_header_t) + 4,
                                 offsetof(nonnullable_handle_message_layout, inline_struct.handle) -
                                     sizeof(fidl_message_header_t) + 4,
                                 0xffffffff),

};
const FidlCodedStruct nonnullable_channel_message_type = {
    .tag = kFidlTypeStruct,
    .element_count = ArrayCount(nonnullable_channel_message_elements),
    .size_v1 = sizeof(nonnullable_handle_inline_data) - sizeof(fidl_message_header_t),
    .size_v2 = sizeof(nonnullable_handle_inline_data) - sizeof(fidl_message_header_t),
    .elements = nonnullable_channel_message_elements,
    .name = "nonnullable_channel_message",
};

static const FidlStructElement multiple_nonnullable_handles_fields[] = {
    FidlStructElement::Field(
        &nonnullable_handle,
        offsetof(multiple_nonnullable_handles_message_layout, inline_struct.handle_0) -
            sizeof(fidl_message_header_t),
        offsetof(multiple_nonnullable_handles_message_layout, inline_struct.handle_0) -
            sizeof(fidl_message_header_t),
        kFidlIsResource_Resource),
    FidlStructElement::Field(
        &nonnullable_channel_handle,
        offsetof(multiple_nonnullable_handles_message_layout, inline_struct.handle_1) -
            sizeof(fidl_message_header_t),
        offsetof(multiple_nonnullable_handles_message_layout, inline_struct.handle_1) -
            sizeof(fidl_message_header_t),
        kFidlIsResource_Resource),
    FidlStructElement::Field(
        &nonnullable_vmo_handle,
        offsetof(multiple_nonnullable_handles_message_layout, inline_struct.handle_2) -
            sizeof(fidl_message_header_t),
        offsetof(multiple_nonnullable_handles_message_layout, inline_struct.handle_2) -
            sizeof(fidl_message_header_t),
        kFidlIsResource_Resource),
};
const FidlCodedStruct multiple_nonnullable_handles_message_type = {
    .tag = kFidlTypeStruct,
    .element_count = ArrayCount(multiple_nonnullable_handles_fields),
    .size_v1 = sizeof(multiple_nonnullable_handles_inline_data) - sizeof(fidl_message_header_t),
    .size_v2 = sizeof(multiple_nonnullable_handles_inline_data) - sizeof(fidl_message_header_t),
    .elements = multiple_nonnullable_handles_fields,
    .name = "multiple_nonnullable_handles_message",
};

static const FidlStructElement nullable_handle_fields[] = {
    FidlStructElement::Field(&nullable_handle,
                             offsetof(nullable_handle_message_layout, inline_struct.handle) -
                                 sizeof(fidl_message_header_t),
                             offsetof(nullable_handle_message_layout, inline_struct.handle) -
                                 sizeof(fidl_message_header_t),
                             kFidlIsResource_Resource),
    FidlStructElement::Padding32(offsetof(nonnullable_handle_message_layout, inline_struct.handle) -
                                     sizeof(fidl_message_header_t) + 4,
                                 offsetof(nonnullable_handle_message_layout, inline_struct.handle) -
                                     sizeof(fidl_message_header_t) + 4,
                                 0xffffffff),
};
const FidlCodedStruct nullable_handle_message_type = {
    .tag = kFidlTypeStruct,
    .element_count = ArrayCount(nullable_handle_fields),
    .size_v1 = sizeof(nullable_handle_inline_data) - sizeof(fidl_message_header_t),
    .size_v2 = sizeof(nullable_handle_inline_data) - sizeof(fidl_message_header_t),
    .elements = nullable_handle_fields,
    .name = "nullable_handle_message",
};

static const FidlStructElement multiple_nullable_handles_fields[] = {
    FidlStructElement::Field(
        &nullable_handle,
        offsetof(multiple_nullable_handles_message_layout, inline_struct.handle_0) -
            sizeof(fidl_message_header_t),
        offsetof(multiple_nullable_handles_message_layout, inline_struct.handle_0) -
            sizeof(fidl_message_header_t),
        kFidlIsResource_Resource),
    FidlStructElement::Field(
        &nullable_channel_handle,
        offsetof(multiple_nullable_handles_message_layout, inline_struct.handle_1) -
            sizeof(fidl_message_header_t),
        offsetof(multiple_nullable_handles_message_layout, inline_struct.handle_1) -
            sizeof(fidl_message_header_t),
        kFidlIsResource_Resource),
    FidlStructElement::Field(
        &nullable_vmo_handle,
        offsetof(multiple_nullable_handles_message_layout, inline_struct.handle_2) -
            sizeof(fidl_message_header_t),
        offsetof(multiple_nullable_handles_message_layout, inline_struct.handle_2) -
            sizeof(fidl_message_header_t),
        kFidlIsResource_Resource),
};
const FidlCodedStruct multiple_nullable_handles_message_type = {
    .tag = kFidlTypeStruct,
    .element_count = ArrayCount(multiple_nullable_handles_fields),
    .size_v1 = sizeof(multiple_nullable_handles_inline_data) - sizeof(fidl_message_header_t),
    .size_v2 = sizeof(multiple_nullable_handles_inline_data) - sizeof(fidl_message_header_t),
    .elements = multiple_nullable_handles_fields,
    .name = "multiple_nullable_handles_message",
};

// Array messages.
static const FidlStructElement array_of_nonnullable_handles_fields[] = {
    FidlStructElement::Field(
        &array_of_four_nonnullable_handles,
        offsetof(array_of_nonnullable_handles_message_layout, inline_struct.handles) -
            sizeof(fidl_message_header_t),
        offsetof(array_of_nonnullable_handles_message_layout, inline_struct.handles) -
            sizeof(fidl_message_header_t),
        kFidlIsResource_Resource),
};
const FidlCodedStruct array_of_nonnullable_handles_message_type = {
    .tag = kFidlTypeStruct,
    .element_count = ArrayCount(array_of_nonnullable_handles_fields),
    .size_v1 = sizeof(array_of_nonnullable_handles_inline_data) - sizeof(fidl_message_header_t),
    .size_v2 = sizeof(array_of_nonnullable_handles_inline_data) - sizeof(fidl_message_header_t),
    .elements = array_of_nonnullable_handles_fields,
    .name = "array_of_nonnullable_handles_message",
};

static const FidlStructElement array_of_nullable_handles_fields[] = {
    FidlStructElement::Field(
        &array_of_five_nullable_handles,
        offsetof(array_of_nullable_handles_message_layout, inline_struct.handles) -
            sizeof(fidl_message_header_t),
        offsetof(array_of_nullable_handles_message_layout, inline_struct.handles) -
            sizeof(fidl_message_header_t),
        kFidlIsResource_Resource),
};
const FidlCodedStruct array_of_nullable_handles_message_type = {
    .tag = kFidlTypeStruct,
    .element_count = ArrayCount(array_of_nullable_handles_fields),
    .size_v1 = sizeof(array_of_nullable_handles_inline_data) - sizeof(fidl_message_header_t),
    .size_v2 = sizeof(array_of_nullable_handles_inline_data) - sizeof(fidl_message_header_t),
    .elements = array_of_nullable_handles_fields,
    .name = "array_of_nullable_handles_message",
};

static const FidlStructElement array_of_array_of_nonnullable_handles_fields[] = {
    FidlStructElement::Field(
        &array_of_three_arrays_of_four_nonnullable_handles,
        offsetof(array_of_array_of_nonnullable_handles_message_layout, inline_struct.handles) -
            sizeof(fidl_message_header_t),
        offsetof(array_of_array_of_nonnullable_handles_message_layout, inline_struct.handles) -
            sizeof(fidl_message_header_t),
        true),
};
const FidlCodedStruct array_of_array_of_nonnullable_handles_message_type = {
    .tag = kFidlTypeStruct,
    .element_count = ArrayCount(array_of_array_of_nonnullable_handles_fields),
    .size_v1 =
        sizeof(array_of_array_of_nonnullable_handles_inline_data) - sizeof(fidl_message_header_t),
    .size_v2 =
        sizeof(array_of_array_of_nonnullable_handles_inline_data) - sizeof(fidl_message_header_t),
    .elements = array_of_array_of_nonnullable_handles_fields,
    .name = "array_of_array_of_nonnullable_handles_message",
};

static const FidlStructElement out_of_line_fields[] = {
    FidlStructElement::Field(
        &array_of_four_nonnullable_handles, offsetof(array_of_nonnullable_handles, handles),
        offsetof(array_of_nonnullable_handles, handles), kFidlIsResource_Resource),

};
static const FidlCodedStruct out_of_line_type = {
    .tag = kFidlTypeStruct,
    .element_count = ArrayCount(out_of_line_fields),
    .size_v1 = sizeof(array_of_nonnullable_handles),
    .size_v2 = sizeof(array_of_nonnullable_handles),
    .elements = out_of_line_fields,
    .name = "out_of_line",
};
static const FidlCodedStructPointer out_of_line_pointer_type = {
    .tag = kFidlTypeStructPointer,
    .struct_type = &out_of_line_type.coded_struct(),
};

static const FidlStructElement out_of_line_array_of_nonnullable_handles_fields[] = {
    FidlStructElement::Field(&out_of_line_pointer_type,
                             offsetof(out_of_line_array_of_nonnullable_handles_message_layout,
                                      inline_struct.maybe_array) -
                                 sizeof(fidl_message_header_t),
                             offsetof(out_of_line_array_of_nonnullable_handles_message_layout,
                                      inline_struct.maybe_array) -
                                 sizeof(fidl_message_header_t),
                             true),
};
const FidlCodedStruct out_of_line_array_of_nonnullable_handles_message_type = {
    .tag = kFidlTypeStruct,
    .element_count = ArrayCount(out_of_line_array_of_nonnullable_handles_fields),
    .size_v1 = sizeof(out_of_line_array_of_nonnullable_handles_inline_data) -
               sizeof(fidl_message_header_t),
    .size_v2 = sizeof(out_of_line_array_of_nonnullable_handles_inline_data) -
               sizeof(fidl_message_header_t),
    .elements = out_of_line_array_of_nonnullable_handles_fields,
    .name = "out_of_line_array_of_nonnullable_handles_message",
};

// String messages.
static const FidlStructElement unbounded_nonnullable_string_fields[] = {
    FidlStructElement::Field(
        &unbounded_nonnullable_string,
        offsetof(unbounded_nonnullable_string_message_layout, inline_struct.string) -
            sizeof(fidl_message_header_t),
        offsetof(unbounded_nonnullable_string_message_layout, inline_struct.string) -
            sizeof(fidl_message_header_t),
        kFidlIsResource_NotResource),

};
const FidlCodedStruct unbounded_nonnullable_string_message_type = {
    .tag = kFidlTypeStruct,
    .element_count = ArrayCount(unbounded_nonnullable_string_fields),
    .size_v1 = sizeof(unbounded_nonnullable_string_inline_data) - sizeof(fidl_message_header_t),
    .size_v2 = sizeof(unbounded_nonnullable_string_inline_data) - sizeof(fidl_message_header_t),
    .elements = unbounded_nonnullable_string_fields,
    .name = "unbounded_nonnullable_string_message",
};

static const FidlStructElement unbounded_nullable_string_fields[] = {
    FidlStructElement::Field(
        &unbounded_nullable_string,
        offsetof(unbounded_nullable_string_message_layout, inline_struct.string) -
            sizeof(fidl_message_header_t),
        offsetof(unbounded_nullable_string_message_layout, inline_struct.string) -
            sizeof(fidl_message_header_t),
        kFidlIsResource_NotResource),

};
const FidlCodedStruct unbounded_nullable_string_message_type = {
    .tag = kFidlTypeStruct,
    .element_count = ArrayCount(unbounded_nullable_string_fields),
    .size_v1 = sizeof(unbounded_nullable_string_inline_data) - sizeof(fidl_message_header_t),
    .size_v2 = sizeof(unbounded_nullable_string_inline_data) - sizeof(fidl_message_header_t),
    .elements = unbounded_nullable_string_fields,
    .name = "unbounded_nullable_string_message",
};

static const FidlStructElement bounded_32_nonnullable_string_fields[] = {
    FidlStructElement::Field(
        &bounded_32_nonnullable_string,
        offsetof(bounded_32_nonnullable_string_message_layout, inline_struct.string) -
            sizeof(fidl_message_header_t),
        offsetof(bounded_32_nonnullable_string_message_layout, inline_struct.string) -
            sizeof(fidl_message_header_t),
        kFidlIsResource_NotResource),

};
const FidlCodedStruct bounded_32_nonnullable_string_message_type = {
    .tag = kFidlTypeStruct,
    .element_count = ArrayCount(bounded_32_nonnullable_string_fields),
    .size_v1 = sizeof(bounded_32_nonnullable_string_inline_data) - sizeof(fidl_message_header_t),
    .size_v2 = sizeof(bounded_32_nonnullable_string_inline_data) - sizeof(fidl_message_header_t),
    .elements = bounded_32_nonnullable_string_fields,
    .name = "bounded_32_nonnullable_string_message",
};

static const FidlStructElement bounded_32_nullable_string_fields[] = {
    FidlStructElement::Field(
        &bounded_32_nullable_string,
        offsetof(bounded_32_nullable_string_message_layout, inline_struct.string) -
            sizeof(fidl_message_header_t),
        offsetof(bounded_32_nullable_string_message_layout, inline_struct.string) -
            sizeof(fidl_message_header_t),
        kFidlIsResource_NotResource),
};
const FidlCodedStruct bounded_32_nullable_string_message_type = {
    .tag = kFidlTypeStruct,
    .element_count = ArrayCount(bounded_32_nullable_string_fields),
    .size_v1 = sizeof(bounded_32_nullable_string_inline_data) - sizeof(fidl_message_header_t),
    .size_v2 = sizeof(bounded_32_nullable_string_inline_data) - sizeof(fidl_message_header_t),
    .elements = bounded_32_nullable_string_fields,
    .name = "bounded_32_nullable_string_message",
};

static const FidlStructElement multiple_nonnullable_strings_fields[] = {
    FidlStructElement::Field(
        &bounded_32_nonnullable_string,
        offsetof(multiple_nonnullable_strings_message_layout, inline_struct.string) -
            sizeof(fidl_message_header_t),
        offsetof(multiple_nonnullable_strings_message_layout, inline_struct.string) -
            sizeof(fidl_message_header_t),
        kFidlIsResource_NotResource),

    FidlStructElement::Field(
        &bounded_32_nonnullable_string,
        offsetof(multiple_nonnullable_strings_message_layout, inline_struct.string2) -
            sizeof(fidl_message_header_t),
        offsetof(multiple_nonnullable_strings_message_layout, inline_struct.string2) -
            sizeof(fidl_message_header_t),
        kFidlIsResource_NotResource),
};
const FidlCodedStruct multiple_nonnullable_strings_message_type = {
    .tag = kFidlTypeStruct,
    .element_count = ArrayCount(multiple_nonnullable_strings_fields),
    .size_v1 = sizeof(multiple_nonnullable_strings_inline_data) - sizeof(fidl_message_header_t),
    .size_v2 = sizeof(multiple_nonnullable_strings_inline_data) - sizeof(fidl_message_header_t),
    .elements = multiple_nonnullable_strings_fields,
    .name = "multiple_nonnullable_strings_message",
};

static const FidlStructElement multiple_nullable_strings_fields[] = {
    FidlStructElement::Field(
        &bounded_32_nullable_string,
        offsetof(multiple_nullable_strings_message_layout, inline_struct.string) -
            sizeof(fidl_message_header_t),
        offsetof(multiple_nullable_strings_message_layout, inline_struct.string) -
            sizeof(fidl_message_header_t),
        kFidlIsResource_NotResource),

    FidlStructElement::Field(
        &bounded_32_nullable_string,
        offsetof(multiple_nullable_strings_message_layout, inline_struct.string2) -
            sizeof(fidl_message_header_t),
        offsetof(multiple_nullable_strings_message_layout, inline_struct.string2) -
            sizeof(fidl_message_header_t),
        kFidlIsResource_NotResource),
};
const FidlCodedStruct multiple_nullable_strings_message_type = {
    .tag = kFidlTypeStruct,
    .element_count = ArrayCount(multiple_nullable_strings_fields),
    .size_v1 = sizeof(multiple_nullable_strings_inline_data) - sizeof(fidl_message_header_t),
    .size_v2 = sizeof(multiple_nullable_strings_inline_data) - sizeof(fidl_message_header_t),
    .elements = multiple_nullable_strings_fields,
    .name = "multiple_nullable_strings_message",
};

static const FidlStructElement multiple_short_nonnullable_strings_fields[] = {
    FidlStructElement::Field(
        &bounded_4_nonnullable_string,
        offsetof(multiple_short_nonnullable_strings_message_layout, inline_struct.string) -
            sizeof(fidl_message_header_t),
        offsetof(multiple_short_nonnullable_strings_message_layout, inline_struct.string) -
            sizeof(fidl_message_header_t),
        kFidlIsResource_NotResource),

    FidlStructElement::Field(
        &bounded_32_nonnullable_string,
        offsetof(multiple_short_nonnullable_strings_message_layout, inline_struct.string2) -
            sizeof(fidl_message_header_t),
        offsetof(multiple_short_nonnullable_strings_message_layout, inline_struct.string2) -
            sizeof(fidl_message_header_t),
        kFidlIsResource_NotResource),
};
const FidlCodedStruct multiple_short_nonnullable_strings_message_type = {
    .tag = kFidlTypeStruct,
    .element_count = ArrayCount(multiple_short_nonnullable_strings_fields),
    .size_v1 =
        sizeof(multiple_short_nonnullable_strings_inline_data) - sizeof(fidl_message_header_t),
    .size_v2 =
        sizeof(multiple_short_nonnullable_strings_inline_data) - sizeof(fidl_message_header_t),
    .elements = multiple_short_nonnullable_strings_fields,
    .name = "multiple_short_nonnullable_strings_message",
};

static const FidlStructElement multiple_short_nullable_strings_fields[] = {
    FidlStructElement::Field(
        &bounded_4_nullable_string,
        offsetof(multiple_short_nullable_strings_message_layout, inline_struct.string) -
            sizeof(fidl_message_header_t),
        offsetof(multiple_short_nullable_strings_message_layout, inline_struct.string) -
            sizeof(fidl_message_header_t),
        kFidlIsResource_NotResource),

    FidlStructElement::Field(
        &bounded_32_nullable_string,
        offsetof(multiple_short_nullable_strings_message_layout, inline_struct.string2) -
            sizeof(fidl_message_header_t),
        offsetof(multiple_short_nullable_strings_message_layout, inline_struct.string2) -
            sizeof(fidl_message_header_t),
        kFidlIsResource_NotResource),
};
const FidlCodedStruct multiple_short_nullable_strings_message_type = {
    .tag = kFidlTypeStruct,
    .element_count = ArrayCount(multiple_short_nullable_strings_fields),
    .size_v1 = sizeof(multiple_short_nullable_strings_inline_data) - sizeof(fidl_message_header_t),
    .size_v2 = sizeof(multiple_short_nullable_strings_inline_data) - sizeof(fidl_message_header_t),
    .elements = multiple_short_nullable_strings_fields,
    .name = "multiple_short_nullable_strings_message",
};

// Vector messages.
static const FidlStructElement unbounded_nonnullable_vector_of_handles_fields[] = {
    FidlStructElement::Field(
        &unbounded_nonnullable_vector_of_handles,
        offsetof(unbounded_nonnullable_vector_of_handles_message_layout, inline_struct.vector) -
            sizeof(fidl_message_header_t),
        offsetof(unbounded_nonnullable_vector_of_handles_message_layout, inline_struct.vector) -
            sizeof(fidl_message_header_t),
        true),

};
const FidlCodedStruct unbounded_nonnullable_vector_of_handles_message_type = {
    .tag = kFidlTypeStruct,
    .element_count = ArrayCount(unbounded_nonnullable_vector_of_handles_fields),
    .size_v1 =
        sizeof(unbounded_nonnullable_vector_of_handles_inline_data) - sizeof(fidl_message_header_t),
    .size_v2 =
        sizeof(unbounded_nonnullable_vector_of_handles_inline_data) - sizeof(fidl_message_header_t),
    .elements = unbounded_nonnullable_vector_of_handles_fields,
    .name = "unbounded_nonnullable_vector_of_handles_message",
};

static const FidlStructElement unbounded_nullable_vector_of_handles_fields[] = {
    FidlStructElement::Field(
        &unbounded_nullable_vector_of_handles,
        offsetof(unbounded_nullable_vector_of_handles_message_layout, inline_struct.vector) -
            sizeof(fidl_message_header_t),
        offsetof(unbounded_nullable_vector_of_handles_message_layout, inline_struct.vector) -
            sizeof(fidl_message_header_t),
        kFidlIsResource_Resource),

};
const FidlCodedStruct unbounded_nullable_vector_of_handles_message_type = {
    .tag = kFidlTypeStruct,
    .element_count = ArrayCount(unbounded_nullable_vector_of_handles_fields),
    .size_v1 =
        sizeof(unbounded_nullable_vector_of_handles_inline_data) - sizeof(fidl_message_header_t),
    .size_v2 =
        sizeof(unbounded_nullable_vector_of_handles_inline_data) - sizeof(fidl_message_header_t),
    .elements = unbounded_nullable_vector_of_handles_fields,
    .name = "unbounded_nullable_vector_of_handles_message",
};

static const FidlStructElement bounded_32_nonnullable_vector_of_handles_fields[] = {
    FidlStructElement::Field(
        &bounded_32_nonnullable_vector_of_handles,
        offsetof(bounded_32_nonnullable_vector_of_handles_message_layout, inline_struct.vector) -
            sizeof(fidl_message_header_t),
        offsetof(bounded_32_nonnullable_vector_of_handles_message_layout, inline_struct.vector) -
            sizeof(fidl_message_header_t),
        true),

};
const FidlCodedStruct bounded_32_nonnullable_vector_of_handles_message_type = {
    .tag = kFidlTypeStruct,
    .element_count = ArrayCount(bounded_32_nonnullable_vector_of_handles_fields),
    .size_v1 = sizeof(bounded_32_nonnullable_vector_of_handles_inline_data) -
               sizeof(fidl_message_header_t),
    .size_v2 = sizeof(bounded_32_nonnullable_vector_of_handles_inline_data) -
               sizeof(fidl_message_header_t),
    .elements = bounded_32_nonnullable_vector_of_handles_fields,
    .name = "bounded_32_nonnullable_vector_of_handles_message",
};

static const FidlStructElement bounded_32_nullable_vector_of_handles_fields[] = {
    FidlStructElement::Field(
        &bounded_32_nullable_vector_of_handles,
        offsetof(bounded_32_nullable_vector_of_handles_message_layout, inline_struct.vector) -
            sizeof(fidl_message_header_t),
        offsetof(bounded_32_nullable_vector_of_handles_message_layout, inline_struct.vector) -
            sizeof(fidl_message_header_t),
        kFidlIsResource_Resource),

};
const FidlCodedStruct bounded_32_nullable_vector_of_handles_message_type = {
    .tag = kFidlTypeStruct,
    .element_count = ArrayCount(bounded_32_nullable_vector_of_handles_fields),
    .size_v1 =
        sizeof(bounded_32_nullable_vector_of_handles_inline_data) - sizeof(fidl_message_header_t),
    .size_v2 =
        sizeof(bounded_32_nullable_vector_of_handles_inline_data) - sizeof(fidl_message_header_t),
    .elements = bounded_32_nullable_vector_of_handles_fields,
    .name = "bounded_32_nullable_vector_of_handles_message",
};

static const FidlStructElement multiple_nonnullable_vectors_of_handles_fields[] = {
    FidlStructElement::Field(
        &bounded_2_nonnullable_vector_of_handles,
        offsetof(multiple_nonnullable_vectors_of_handles_message_layout, inline_struct.vector) -
            sizeof(fidl_message_header_t),
        offsetof(multiple_nonnullable_vectors_of_handles_message_layout, inline_struct.vector) -
            sizeof(fidl_message_header_t),
        true),
    FidlStructElement::Field(
        &unbounded_nonnullable_vector_of_handles,
        offsetof(multiple_nonnullable_vectors_of_handles_message_layout, inline_struct.vector2) -
            sizeof(fidl_message_header_t),
        offsetof(multiple_nonnullable_vectors_of_handles_message_layout, inline_struct.vector2) -
            sizeof(fidl_message_header_t),
        true),
};
const FidlCodedStruct multiple_nonnullable_vectors_of_handles_message_type = {
    .tag = kFidlTypeStruct,
    .element_count = ArrayCount(multiple_nonnullable_vectors_of_handles_fields),
    .size_v1 =
        sizeof(multiple_nonnullable_vectors_of_handles_inline_data) - sizeof(fidl_message_header_t),
    .size_v2 =
        sizeof(multiple_nonnullable_vectors_of_handles_inline_data) - sizeof(fidl_message_header_t),
    .elements = multiple_nonnullable_vectors_of_handles_fields,
    .name = "multiple_nonnullable_vectors_of_handles_message",
};

static const FidlStructElement multiple_nullable_vectors_of_handles_fields[] = {
    FidlStructElement::Field(
        &bounded_2_nullable_vector_of_handles,
        offsetof(multiple_nullable_vectors_of_handles_message_layout, inline_struct.vector) -
            sizeof(fidl_message_header_t),
        offsetof(multiple_nullable_vectors_of_handles_message_layout, inline_struct.vector) -
            sizeof(fidl_message_header_t),
        kFidlIsResource_Resource),
    FidlStructElement::Field(
        &unbounded_nullable_vector_of_handles,
        offsetof(multiple_nullable_vectors_of_handles_message_layout, inline_struct.vector2) -
            sizeof(fidl_message_header_t),
        offsetof(multiple_nullable_vectors_of_handles_message_layout, inline_struct.vector2) -
            sizeof(fidl_message_header_t),
        kFidlIsResource_Resource),
};
const FidlCodedStruct multiple_nullable_vectors_of_handles_message_type = {
    .tag = kFidlTypeStruct,
    .element_count = ArrayCount(multiple_nullable_vectors_of_handles_fields),
    .size_v1 =
        sizeof(multiple_nullable_vectors_of_handles_inline_data) - sizeof(fidl_message_header_t),
    .size_v2 =
        sizeof(multiple_nullable_vectors_of_handles_inline_data) - sizeof(fidl_message_header_t),
    .elements = multiple_nullable_vectors_of_handles_fields,
    .name = "multiple_nullable_vectors_of_handles_message",
};

static const FidlStructElement unbounded_nonnullable_vector_of_uint32_fields[] = {
    FidlStructElement::Field(
        &unbounded_nonnullable_vector_of_uint32,
        offsetof(unbounded_nonnullable_vector_of_uint32_message_layout, inline_struct.vector) -
            sizeof(fidl_message_header_t),
        offsetof(unbounded_nonnullable_vector_of_uint32_message_layout, inline_struct.vector) -
            sizeof(fidl_message_header_t),
        false),

};
const FidlCodedStruct unbounded_nonnullable_vector_of_uint32_message_type = {
    .tag = kFidlTypeStruct,
    .element_count = ArrayCount(unbounded_nonnullable_vector_of_uint32_fields),
    .size_v1 =
        sizeof(unbounded_nonnullable_vector_of_uint32_inline_data) - sizeof(fidl_message_header_t),
    .size_v2 =
        sizeof(unbounded_nonnullable_vector_of_uint32_inline_data) - sizeof(fidl_message_header_t),
    .elements = unbounded_nonnullable_vector_of_uint32_fields,
    .name = "unbounded_nonnullable_vector_of_uint32_message",
};

static const FidlStructElement unbounded_nullable_vector_of_uint32_fields[] = {
    FidlStructElement::Field(
        &unbounded_nullable_vector_of_uint32,
        offsetof(unbounded_nullable_vector_of_uint32_message_layout, inline_struct.vector) -
            sizeof(fidl_message_header_t),
        offsetof(unbounded_nullable_vector_of_uint32_message_layout, inline_struct.vector) -
            sizeof(fidl_message_header_t),
        kFidlIsResource_NotResource),

};
const FidlCodedStruct unbounded_nullable_vector_of_uint32_message_type = {
    .tag = kFidlTypeStruct,
    .element_count = ArrayCount(unbounded_nullable_vector_of_uint32_fields),
    .size_v1 =
        sizeof(unbounded_nullable_vector_of_uint32_inline_data) - sizeof(fidl_message_header_t),
    .size_v2 =
        sizeof(unbounded_nullable_vector_of_uint32_inline_data) - sizeof(fidl_message_header_t),
    .elements = unbounded_nullable_vector_of_uint32_fields,
    .name = "unbounded_nullable_vector_of_uint32_message",
};

static const FidlStructElement bounded_32_nonnullable_vector_of_uint32_fields[] = {
    FidlStructElement::Field(
        &bounded_32_nonnullable_vector_of_uint32,
        offsetof(bounded_32_nonnullable_vector_of_uint32_message_layout, inline_struct.vector) -
            sizeof(fidl_message_header_t),
        offsetof(bounded_32_nonnullable_vector_of_uint32_message_layout, inline_struct.vector) -
            sizeof(fidl_message_header_t),
        false),

};
const FidlCodedStruct bounded_32_nonnullable_vector_of_uint32_message_type = {
    .tag = kFidlTypeStruct,
    .element_count = ArrayCount(bounded_32_nonnullable_vector_of_uint32_fields),
    .size_v1 =
        sizeof(bounded_32_nonnullable_vector_of_uint32_inline_data) - sizeof(fidl_message_header_t),
    .size_v2 =
        sizeof(bounded_32_nonnullable_vector_of_uint32_inline_data) - sizeof(fidl_message_header_t),
    .elements = bounded_32_nonnullable_vector_of_uint32_fields,
    .name = "bounded_32_nonnullable_vector_of_uint32_message",
};

static const FidlStructElement bounded_32_nullable_vector_of_uint32_fields[] = {
    FidlStructElement::Field(
        &bounded_32_nullable_vector_of_uint32,
        offsetof(bounded_32_nullable_vector_of_uint32_message_layout, inline_struct.vector) -
            sizeof(fidl_message_header_t),
        offsetof(bounded_32_nullable_vector_of_uint32_message_layout, inline_struct.vector) -
            sizeof(fidl_message_header_t),
        kFidlIsResource_NotResource),

};
const FidlCodedStruct bounded_32_nullable_vector_of_uint32_message_type = {
    .tag = kFidlTypeStruct,
    .element_count = ArrayCount(bounded_32_nullable_vector_of_uint32_fields),
    .size_v1 =
        sizeof(bounded_32_nullable_vector_of_uint32_inline_data) - sizeof(fidl_message_header_t),
    .size_v2 =
        sizeof(bounded_32_nullable_vector_of_uint32_inline_data) - sizeof(fidl_message_header_t),
    .elements = bounded_32_nullable_vector_of_uint32_fields,
    .name = "bounded_32_nullable_vector_of_uint32_message",
};

static const FidlStructElement multiple_nonnullable_vectors_of_uint32_fields[] = {
    FidlStructElement::Field(
        &bounded_2_nonnullable_vector_of_uint32,
        offsetof(multiple_nonnullable_vectors_of_uint32_message_layout, inline_struct.vector) -
            sizeof(fidl_message_header_t),
        offsetof(multiple_nonnullable_vectors_of_uint32_message_layout, inline_struct.vector) -
            sizeof(fidl_message_header_t),
        false),
    FidlStructElement::Field(
        &unbounded_nonnullable_vector_of_uint32,
        offsetof(multiple_nonnullable_vectors_of_uint32_message_layout, inline_struct.vector2) -
            sizeof(fidl_message_header_t),
        offsetof(multiple_nonnullable_vectors_of_uint32_message_layout, inline_struct.vector2) -
            sizeof(fidl_message_header_t),
        false),
};
const FidlCodedStruct multiple_nonnullable_vectors_of_uint32_message_type = {
    .tag = kFidlTypeStruct,
    .element_count = ArrayCount(multiple_nonnullable_vectors_of_uint32_fields),
    .size_v1 =
        sizeof(multiple_nonnullable_vectors_of_uint32_inline_data) - sizeof(fidl_message_header_t),
    .size_v2 =
        sizeof(multiple_nonnullable_vectors_of_uint32_inline_data) - sizeof(fidl_message_header_t),
    .elements = multiple_nonnullable_vectors_of_uint32_fields,
    .name = "multiple_nonnullable_vectors_of_uint32_message",
};

static const FidlStructElement multiple_nullable_vectors_of_uint32_fields[] = {
    FidlStructElement::Field(
        &bounded_2_nullable_vector_of_uint32,
        offsetof(multiple_nullable_vectors_of_uint32_message_layout, inline_struct.vector) -
            sizeof(fidl_message_header_t),
        offsetof(multiple_nullable_vectors_of_uint32_message_layout, inline_struct.vector) -
            sizeof(fidl_message_header_t),
        kFidlIsResource_NotResource),
    FidlStructElement::Field(
        &unbounded_nullable_vector_of_uint32,
        offsetof(multiple_nullable_vectors_of_uint32_message_layout, inline_struct.vector2) -
            sizeof(fidl_message_header_t),
        offsetof(multiple_nullable_vectors_of_uint32_message_layout, inline_struct.vector2) -
            sizeof(fidl_message_header_t),
        kFidlIsResource_NotResource),
};
const FidlCodedStruct multiple_nullable_vectors_of_uint32_message_type = {
    .tag = kFidlTypeStruct,
    .element_count = ArrayCount(multiple_nullable_vectors_of_uint32_fields),
    .size_v1 =
        sizeof(multiple_nullable_vectors_of_uint32_inline_data) - sizeof(fidl_message_header_t),
    .size_v2 =
        sizeof(multiple_nullable_vectors_of_uint32_inline_data) - sizeof(fidl_message_header_t),
    .elements = multiple_nullable_vectors_of_uint32_fields,
    .name = "multiple_nullable_vectors_of_uint32_message",
};

// Struct messages.
static const FidlStructElement struct_level_3_fields[] = {
    FidlStructElement::Field(&nonnullable_handle, offsetof(struct_level_3, handle_3),
                             offsetof(struct_level_3, handle_3), kFidlIsResource_Resource),
};
static const FidlCodedStruct struct_level_3_struct = {
    .tag = kFidlTypeStruct,
    .element_count = ArrayCount(struct_level_3_fields),
    .size_v1 = sizeof(struct_level_3),
    .size_v2 = sizeof(struct_level_3),
    .elements = struct_level_3_fields,
    .name = "struct_level_3",
};
static const FidlStructElement struct_level_2_fields[] = {
    FidlStructElement::Field(&struct_level_3_struct, offsetof(struct_level_2, l3),
                             offsetof(struct_level_2, l3), kFidlIsResource_NotResource),
    FidlStructElement::Field(&nonnullable_handle, offsetof(struct_level_2, handle_2),
                             offsetof(struct_level_2, handle_2), kFidlIsResource_Resource),
    FidlStructElement::Padding32(offsetof(struct_level_2, handle_2) + 4,
                                 offsetof(struct_level_2, handle_2) + 4, 0xffffffff),
};
static const FidlCodedStruct struct_level_2_struct = {
    .tag = kFidlTypeStruct,
    .element_count = ArrayCount(struct_level_2_fields),
    .size_v1 = sizeof(struct_level_2),
    .size_v2 = sizeof(struct_level_2),
    .elements = struct_level_2_fields,
    .name = "struct_level_2",
};
static const FidlStructElement struct_level_1_fields[] = {
    FidlStructElement::Field(&nonnullable_handle, offsetof(struct_level_1, handle_1),
                             offsetof(struct_level_1, handle_1), kFidlIsResource_Resource),
    FidlStructElement::Padding32(offsetof(struct_level_1, handle_1) + 4,
                                 offsetof(struct_level_1, handle_1) + 4, 0xffffffff),
    FidlStructElement::Field(&struct_level_2_struct, offsetof(struct_level_1, l2),
                             offsetof(struct_level_1, l2), kFidlIsResource_NotResource),
};
static const FidlCodedStruct struct_level_1_struct = {
    .tag = kFidlTypeStruct,
    .element_count = ArrayCount(struct_level_1_fields),
    .size_v1 = sizeof(struct_level_1),
    .size_v2 = sizeof(struct_level_1),
    .elements = struct_level_1_fields,
    .name = "struct_level_1",
};
static const FidlStructElement struct_level_0_fields[] = {
    FidlStructElement::Field(&struct_level_1_struct, offsetof(struct_level_0, l1),
                             offsetof(struct_level_0, l1), kFidlIsResource_NotResource),
    FidlStructElement::Field(&nonnullable_handle, offsetof(struct_level_0, handle_0),
                             offsetof(struct_level_0, handle_0), kFidlIsResource_Resource),
    FidlStructElement::Padding32(offsetof(struct_level_0, handle_0) + 4,
                                 offsetof(struct_level_0, handle_0) + 4, 0xffffffff),
};
const FidlCodedStruct struct_level_0_struct = {
    .tag = kFidlTypeStruct,
    .element_count = ArrayCount(struct_level_1_fields),
    .size_v1 = sizeof(struct_level_0),
    .size_v2 = sizeof(struct_level_0),
    .elements = struct_level_0_fields,
    .name = "struct_level_0",
};
static const FidlStructElement nested_structs_fields[] = {
    FidlStructElement::Field(
        &struct_level_0_struct,
        offsetof(nested_structs_message_layout, inline_struct.l0) - sizeof(fidl_message_header_t),
        offsetof(nested_structs_message_layout, inline_struct.l0) - sizeof(fidl_message_header_t),
        kFidlIsResource_NotResource),

};
const FidlCodedStruct nested_structs_message_type = {
    .tag = kFidlTypeStruct,
    .element_count = ArrayCount(nested_structs_fields),
    .size_v1 = sizeof(nested_structs_inline_data) - sizeof(fidl_message_header_t),
    .size_v2 = sizeof(nested_structs_inline_data) - sizeof(fidl_message_header_t),
    .elements = nested_structs_fields,
    .name = "nested_structs_message",
};

// Struct pointer messages.
static const FidlStructElement struct_ptr_level_3_fields[] = {
    FidlStructElement::Field(&nonnullable_handle, offsetof(struct_ptr_level_3, handle_3),
                             offsetof(struct_ptr_level_3, handle_3), kFidlIsResource_Resource),
};
static const FidlCodedStruct struct_ptr_level_3_struct = {
    .tag = kFidlTypeStruct,
    .element_count = ArrayCount(struct_ptr_level_3_fields),
    .size_v1 = sizeof(struct_ptr_level_3),
    .size_v2 = sizeof(struct_ptr_level_3),
    .elements = struct_ptr_level_3_fields,
    .name = "struct_ptr_level_3",
};
static const FidlCodedStructPointer struct_ptr_level_3_struct_pointer = {
    .tag = kFidlTypeStructPointer,
    .struct_type = &struct_ptr_level_3_struct.coded_struct(),
};
static const FidlStructElement struct_ptr_level_2_fields[] = {
    FidlStructElement::Field(&struct_ptr_level_3_struct_pointer,
                             offsetof(struct_ptr_level_2, l3_present),
                             offsetof(struct_ptr_level_2, l3_present), kFidlIsResource_NotResource),
    FidlStructElement::Field(&struct_ptr_level_3_struct_pointer,
                             offsetof(struct_ptr_level_2, l3_absent),
                             offsetof(struct_ptr_level_2, l3_absent), kFidlIsResource_NotResource),
    FidlStructElement::Field(&struct_ptr_level_3_struct, offsetof(struct_ptr_level_2, l3_inline),
                             offsetof(struct_ptr_level_2, l3_inline), kFidlIsResource_NotResource),
    FidlStructElement::Field(&nonnullable_handle, offsetof(struct_ptr_level_2, handle_2),
                             offsetof(struct_ptr_level_2, handle_2), kFidlIsResource_Resource),
};
static const FidlCodedStruct struct_ptr_level_2_struct = {
    .tag = kFidlTypeStruct,
    .element_count = ArrayCount(struct_ptr_level_2_fields),
    .size_v1 = sizeof(struct_ptr_level_2),
    .size_v2 = sizeof(struct_ptr_level_2),
    .elements = struct_ptr_level_2_fields,
    .name = "struct_ptr_level_2",
};
static const FidlCodedStructPointer struct_ptr_level_2_struct_pointer = {
    .tag = kFidlTypeStructPointer,
    .struct_type = &struct_ptr_level_2_struct.coded_struct(),
};
static const FidlStructElement struct_ptr_level_1_fields[] = {
    FidlStructElement::Field(&nonnullable_handle, offsetof(struct_ptr_level_1, handle_1),
                             offsetof(struct_ptr_level_1, handle_1), kFidlIsResource_Resource),
    FidlStructElement::Padding32(offsetof(struct_ptr_level_1, handle_1) + 4,
                                 offsetof(struct_ptr_level_1, handle_1) + 4, 0xffffffff),
    FidlStructElement::Field(&struct_ptr_level_2_struct_pointer,
                             offsetof(struct_ptr_level_1, l2_present),
                             offsetof(struct_ptr_level_1, l2_present), kFidlIsResource_NotResource),
    FidlStructElement::Field(&struct_ptr_level_2_struct, offsetof(struct_ptr_level_1, l2_inline),
                             offsetof(struct_ptr_level_1, l2_inline), kFidlIsResource_NotResource),
    FidlStructElement::Field(&struct_ptr_level_2_struct_pointer,
                             offsetof(struct_ptr_level_1, l2_absent),
                             offsetof(struct_ptr_level_1, l2_absent), kFidlIsResource_NotResource),
};
static const FidlCodedStruct struct_ptr_level_1_struct = {
    .tag = kFidlTypeStruct,
    .element_count = ArrayCount(struct_ptr_level_1_fields),
    .size_v1 = sizeof(struct_ptr_level_1),
    .size_v2 = sizeof(struct_ptr_level_1),
    .elements = struct_ptr_level_1_fields,
    .name = "struct_ptr_level_1",
};
static const FidlCodedStructPointer struct_ptr_level_1_struct_pointer = {
    .tag = kFidlTypeStructPointer,
    .struct_type = &struct_ptr_level_1_struct.coded_struct(),
};
static const FidlStructElement struct_ptr_level_0_fields[] = {
    FidlStructElement::Field(&struct_ptr_level_1_struct_pointer,
                             offsetof(struct_ptr_level_0, l1_absent),
                             offsetof(struct_ptr_level_0, l1_absent), kFidlIsResource_NotResource),
    FidlStructElement::Field(&struct_ptr_level_1_struct, offsetof(struct_ptr_level_0, l1_inline),
                             offsetof(struct_ptr_level_0, l1_inline), kFidlIsResource_NotResource),
    FidlStructElement::Field(&nonnullable_handle, offsetof(struct_ptr_level_0, handle_0),
                             offsetof(struct_ptr_level_0, handle_0), kFidlIsResource_Resource),
    FidlStructElement::Padding32(offsetof(struct_ptr_level_0, handle_0) + 4,
                                 offsetof(struct_ptr_level_0, handle_0) + 4, 0xffffffff),
    FidlStructElement::Field(&struct_ptr_level_1_struct_pointer,
                             offsetof(struct_ptr_level_0, l1_present),
                             offsetof(struct_ptr_level_0, l1_present), kFidlIsResource_NotResource),
};
static const FidlCodedStruct struct_ptr_level_0_struct = {
    .tag = kFidlTypeStruct,
    .element_count = ArrayCount(struct_ptr_level_0_fields),
    .size_v1 = sizeof(struct_ptr_level_0),
    .size_v2 = sizeof(struct_ptr_level_0),
    .elements = struct_ptr_level_0_fields,
    .name = "struct_ptr_level_0",
};
const FidlCodedStructPointer struct_ptr_level_0_struct_pointer = {
    .tag = kFidlTypeStructPointer,
    .struct_type = &struct_ptr_level_0_struct.coded_struct(),
};
static const FidlStructElement nested_struct_ptrs_fields[] = {
    FidlStructElement::Field(
        &struct_ptr_level_0_struct,
        offsetof(nested_struct_ptrs_inline_data, l0_inline) - sizeof(fidl_message_header_t),
        offsetof(nested_struct_ptrs_inline_data, l0_inline) - sizeof(fidl_message_header_t),
        kFidlIsResource_NotResource),
    FidlStructElement::Field(
        &struct_ptr_level_0_struct_pointer,
        offsetof(nested_struct_ptrs_inline_data, l0_absent) - sizeof(fidl_message_header_t),
        offsetof(nested_struct_ptrs_inline_data, l0_absent) - sizeof(fidl_message_header_t),
        kFidlIsResource_NotResource),
    FidlStructElement::Field(
        &struct_ptr_level_0_struct_pointer,
        offsetof(nested_struct_ptrs_inline_data, l0_present) - sizeof(fidl_message_header_t),
        offsetof(nested_struct_ptrs_inline_data, l0_present) - sizeof(fidl_message_header_t),
        kFidlIsResource_NotResource),
};
const FidlCodedStruct nested_struct_ptrs_message_type = {
    .tag = kFidlTypeStruct,
    .element_count = ArrayCount(nested_struct_ptrs_fields),
    .size_v1 = sizeof(nested_struct_ptrs_inline_data) - sizeof(fidl_message_header_t),
    .size_v2 = sizeof(nested_struct_ptrs_inline_data) - sizeof(fidl_message_header_t),
    .elements = nested_struct_ptrs_fields,
    .name = "nested_struct_ptrs_message",
};
