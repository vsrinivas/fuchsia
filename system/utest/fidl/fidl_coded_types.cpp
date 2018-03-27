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

} // namespace

// Handle types.
const fidl_type_t nonnullable_handle =
    fidl_type_t(fidl::FidlCodedHandle(ZX_OBJ_TYPE_NONE, fidl::kNonnullable));
const fidl_type_t nullable_handle =
    fidl_type_t(fidl::FidlCodedHandle(ZX_OBJ_TYPE_NONE, fidl::kNullable));
const fidl_type_t nullable_channel_handle =
    fidl_type_t(fidl::FidlCodedHandle(ZX_OBJ_TYPE_CHANNEL, fidl::kNullable));
const fidl_type_t nullable_vmo_handle =
    fidl_type_t(fidl::FidlCodedHandle(ZX_OBJ_TYPE_VMO, fidl::kNullable));
const fidl_type_t nonnullable_channel_handle =
    fidl_type_t(fidl::FidlCodedHandle(ZX_OBJ_TYPE_CHANNEL, fidl::kNonnullable));
const fidl_type_t nonnullable_vmo_handle =
    fidl_type_t(fidl::FidlCodedHandle(ZX_OBJ_TYPE_VMO, fidl::kNonnullable));

// Array types.
const fidl_type_t array_of_two_nonnullable_handles = fidl_type_t(
    fidl::FidlCodedArray(&nonnullable_handle, 2 * sizeof(zx_handle_t), sizeof(zx_handle_t)));
const fidl_type_t array_of_four_nonnullable_handles = fidl_type_t(
    fidl::FidlCodedArray(&nonnullable_handle, 4 * sizeof(zx_handle_t), sizeof(zx_handle_t)));
const fidl_type_t array_of_five_nullable_handles = fidl_type_t(
    fidl::FidlCodedArray(&nullable_handle, 5 * sizeof(zx_handle_t), sizeof(zx_handle_t)));
const fidl_type_t array_of_three_arrays_of_four_nonnullable_handles =
    fidl_type_t(fidl::FidlCodedArray(&array_of_four_nonnullable_handles,
                                     3 * 4 * sizeof(zx_handle_t), 4 * sizeof(zx_handle_t)));
const fidl_type_t array_of_two_arrays_of_two_nonnullable_handles =
    fidl_type_t(fidl::FidlCodedArray(&array_of_two_nonnullable_handles, 2 * 2 * sizeof(zx_handle_t),
                                     2 * sizeof(zx_handle_t)));

// String types.
const fidl_type_t unbounded_nonnullable_string =
    fidl_type_t(fidl::FidlCodedString(FIDL_MAX_SIZE, fidl::kNonnullable));
const fidl_type_t unbounded_nullable_string =
    fidl_type_t(fidl::FidlCodedString(FIDL_MAX_SIZE, fidl::kNullable));
const fidl_type_t bounded_32_nonnullable_string =
    fidl_type_t(fidl::FidlCodedString(32, fidl::kNonnullable));
const fidl_type_t bounded_32_nullable_string =
    fidl_type_t(fidl::FidlCodedString(32, fidl::kNullable));
const fidl_type_t bounded_4_nonnullable_string =
    fidl_type_t(fidl::FidlCodedString(4, fidl::kNonnullable));
const fidl_type_t bounded_4_nullable_string =
    fidl_type_t(fidl::FidlCodedString(4, fidl::kNullable));

// Vector types.
const fidl_type_t unbounded_nonnullable_vector_of_handles =
    fidl_type_t(fidl::FidlCodedVector(&nonnullable_handle, FIDL_MAX_SIZE, sizeof(zx_handle_t),
                                      fidl::kNonnullable));
const fidl_type_t unbounded_nullable_vector_of_handles = fidl_type_t(fidl::FidlCodedVector(
    &nonnullable_handle, FIDL_MAX_SIZE, sizeof(zx_handle_t), fidl::kNullable));
const fidl_type_t bounded_32_nonnullable_vector_of_handles = fidl_type_t(
    fidl::FidlCodedVector(&nonnullable_handle, 32, sizeof(zx_handle_t), fidl::kNonnullable));
const fidl_type_t bounded_32_nullable_vector_of_handles = fidl_type_t(
    fidl::FidlCodedVector(&nonnullable_handle, 32, sizeof(zx_handle_t), fidl::kNullable));
const fidl_type_t bounded_2_nonnullable_vector_of_handles = fidl_type_t(
    fidl::FidlCodedVector(&nonnullable_handle, 2, sizeof(zx_handle_t), fidl::kNonnullable));
const fidl_type_t bounded_2_nullable_vector_of_handles = fidl_type_t(
    fidl::FidlCodedVector(&nonnullable_handle, 2, sizeof(zx_handle_t), fidl::kNullable));

const fidl_type_t unbounded_nonnullable_vector_of_uint32 =
    fidl_type_t(fidl::FidlCodedVector(nullptr, FIDL_MAX_SIZE, sizeof(uint32_t),
                                      fidl::kNonnullable));
const fidl_type_t unbounded_nullable_vector_of_uint32 = fidl_type_t(fidl::FidlCodedVector(
    nullptr, FIDL_MAX_SIZE, sizeof(uint32_t), fidl::kNullable));
const fidl_type_t bounded_32_nonnullable_vector_of_uint32 = fidl_type_t(
    fidl::FidlCodedVector(nullptr, 32, sizeof(uint32_t), fidl::kNonnullable));
const fidl_type_t bounded_32_nullable_vector_of_uint32 = fidl_type_t(
    fidl::FidlCodedVector(nullptr, 32, sizeof(uint32_t), fidl::kNullable));
const fidl_type_t bounded_2_nonnullable_vector_of_uint32 = fidl_type_t(
    fidl::FidlCodedVector(nullptr, 2, sizeof(uint32_t), fidl::kNonnullable));
const fidl_type_t bounded_2_nullable_vector_of_uint32 = fidl_type_t(
    fidl::FidlCodedVector(nullptr, 2, sizeof(uint32_t), fidl::kNullable));

// Handle messages.
static const fidl::FidlField nonnullable_handle_message_fields[] = {
    fidl::FidlField(&nonnullable_handle,
                    offsetof(nonnullable_handle_message_layout, inline_struct.handle)),
};
const fidl_type_t nonnullable_handle_message_type = fidl_type_t(fidl::FidlCodedStruct(
    nonnullable_handle_message_fields, ArrayCount(nonnullable_handle_message_fields),
    sizeof(nonnullable_handle_inline_data),
    "nonnullable_handle_message"));

static const fidl::FidlField multiple_nonnullable_handles_fields[] = {
    fidl::FidlField(&nonnullable_handle,
                    offsetof(multiple_nonnullable_handles_message_layout, inline_struct.handle_0)),
    fidl::FidlField(&nonnullable_channel_handle,
                    offsetof(multiple_nonnullable_handles_message_layout, inline_struct.handle_1)),
    fidl::FidlField(&nonnullable_vmo_handle,
                    offsetof(multiple_nonnullable_handles_message_layout, inline_struct.handle_2)),
};
const fidl_type_t multiple_nonnullable_handles_message_type = fidl_type_t(fidl::FidlCodedStruct(
    multiple_nonnullable_handles_fields, ArrayCount(multiple_nonnullable_handles_fields),
    sizeof(multiple_nonnullable_handles_inline_data),
    "multiple_nonnullable_handles_message"));

static const fidl::FidlField nullable_handle_fields[] = {
    fidl::FidlField(&nullable_handle,
                    offsetof(nullable_handle_message_layout, inline_struct.handle)),

};
const fidl_type_t nullable_handle_message_type =
    fidl_type_t(fidl::FidlCodedStruct(nullable_handle_fields, ArrayCount(nullable_handle_fields),
                                      sizeof(nullable_handle_inline_data),
                                      "nullable_handle_message"));

static const fidl::FidlField multiple_nullable_handles_fields[] = {
    fidl::FidlField(&nullable_handle,
                    offsetof(multiple_nullable_handles_message_layout, inline_struct.handle_0)),
    fidl::FidlField(&nullable_channel_handle,
                    offsetof(multiple_nullable_handles_message_layout, inline_struct.handle_1)),
    fidl::FidlField(&nullable_vmo_handle,
                    offsetof(multiple_nullable_handles_message_layout, inline_struct.handle_2)),
};
const fidl_type_t multiple_nullable_handles_message_type = fidl_type_t(fidl::FidlCodedStruct(
    multiple_nullable_handles_fields, ArrayCount(multiple_nullable_handles_fields),
    sizeof(multiple_nullable_handles_inline_data),
    "multiple_nullable_handles_message"));

// Array messages.
static const fidl::FidlField array_of_nonnullable_handles_fields[] = {
    fidl::FidlField(&array_of_four_nonnullable_handles,
                    offsetof(array_of_nonnullable_handles_message_layout, inline_struct.handles)),
};
const fidl_type_t array_of_nonnullable_handles_message_type = fidl_type_t(fidl::FidlCodedStruct(
    array_of_nonnullable_handles_fields, ArrayCount(array_of_nonnullable_handles_fields),
    sizeof(array_of_nonnullable_handles_inline_data),
    "array_of_nonnullable_handles_message"));

static const fidl::FidlField array_of_nullable_handles_fields[] = {
    fidl::FidlField(&array_of_five_nullable_handles,
                    offsetof(array_of_nullable_handles_message_layout, inline_struct.handles)),
};
const fidl_type_t array_of_nullable_handles_message_type = fidl_type_t(fidl::FidlCodedStruct(
    array_of_nullable_handles_fields, ArrayCount(array_of_nullable_handles_fields),
    sizeof(array_of_nullable_handles_inline_data),
    "array_of_nullable_handles_message"));

static const fidl::FidlField array_of_array_of_nonnullable_handles_fields[] = {
    fidl::FidlField(
        &array_of_three_arrays_of_four_nonnullable_handles,
        offsetof(array_of_array_of_nonnullable_handles_message_layout, inline_struct.handles)),
};
const fidl_type_t array_of_array_of_nonnullable_handles_message_type =
    fidl_type_t(fidl::FidlCodedStruct(array_of_array_of_nonnullable_handles_fields,
                                      ArrayCount(array_of_array_of_nonnullable_handles_fields),
                                      sizeof(array_of_array_of_nonnullable_handles_inline_data),
                                      "array_of_array_of_nonnullable_handles_message"));

static const fidl::FidlField out_of_line_fields[] = {
    fidl::FidlField(&array_of_four_nonnullable_handles,
                    offsetof(array_of_nonnullable_handles, handles)),

};
static const fidl_type_t out_of_line_type = fidl_type_t(fidl::FidlCodedStruct(
    out_of_line_fields, ArrayCount(out_of_line_fields), sizeof(array_of_nonnullable_handles),
    "out_of_line"));
static const fidl_type_t out_of_line_pointer_type =
    fidl_type_t(fidl::FidlCodedStructPointer(&out_of_line_type.coded_struct));

static const fidl::FidlField out_of_line_array_of_nonnullable_handles_fields[] = {
    fidl::FidlField(&out_of_line_pointer_type,
                    offsetof(out_of_line_array_of_nonnullable_handles_message_layout,
                             inline_struct.maybe_array)),
};
const fidl_type_t out_of_line_array_of_nonnullable_handles_message_type = fidl_type_t(
    fidl::FidlCodedStruct(out_of_line_array_of_nonnullable_handles_fields,
                          ArrayCount(out_of_line_array_of_nonnullable_handles_fields),
                          sizeof(out_of_line_array_of_nonnullable_handles_inline_data),
                          "out_of_line_array_of_nonnullable_handles_message"));

// String messages.
static const fidl::FidlField unbounded_nonnullable_string_fields[] = {
    fidl::FidlField(&unbounded_nonnullable_string,
                    offsetof(unbounded_nonnullable_string_message_layout, inline_struct.string)),

};
const fidl_type_t unbounded_nonnullable_string_message_type = fidl_type_t(fidl::FidlCodedStruct(
    unbounded_nonnullable_string_fields, ArrayCount(unbounded_nonnullable_string_fields),
    sizeof(unbounded_nonnullable_string_inline_data),
    "unbounded_nonnullable_string_message"));

static const fidl::FidlField unbounded_nullable_string_fields[] = {
    fidl::FidlField(&unbounded_nullable_string,
                    offsetof(unbounded_nullable_string_message_layout, inline_struct.string)),

};
const fidl_type_t unbounded_nullable_string_message_type = fidl_type_t(fidl::FidlCodedStruct(
    unbounded_nullable_string_fields, ArrayCount(unbounded_nullable_string_fields),
    sizeof(unbounded_nullable_string_inline_data),
    "unbounded_nullable_string_message"));

static const fidl::FidlField bounded_32_nonnullable_string_fields[] = {
    fidl::FidlField(&bounded_32_nonnullable_string,
                    offsetof(bounded_32_nonnullable_string_message_layout, inline_struct.string)),

};
const fidl_type_t bounded_32_nonnullable_string_message_type = fidl_type_t(fidl::FidlCodedStruct(
    bounded_32_nonnullable_string_fields, ArrayCount(bounded_32_nonnullable_string_fields),
    sizeof(bounded_32_nonnullable_string_inline_data),
    "bounded_32_nonnullable_string_message"));

static const fidl::FidlField bounded_32_nullable_string_fields[] = {
    fidl::FidlField(&bounded_32_nullable_string,
                    offsetof(bounded_32_nullable_string_message_layout, inline_struct.string)),
};
const fidl_type_t bounded_32_nullable_string_message_type = fidl_type_t(fidl::FidlCodedStruct(
    bounded_32_nullable_string_fields, ArrayCount(bounded_32_nullable_string_fields),
    sizeof(bounded_32_nullable_string_inline_data),
    "bounded_32_nullable_string_message"));

static const fidl::FidlField multiple_nonnullable_strings_fields[] = {
    fidl::FidlField(&bounded_32_nonnullable_string,
                    offsetof(multiple_nonnullable_strings_message_layout, inline_struct.string)),

    fidl::FidlField(&bounded_32_nonnullable_string,
                    offsetof(multiple_nonnullable_strings_message_layout, inline_struct.string2)),
};
const fidl_type_t multiple_nonnullable_strings_message_type = fidl_type_t(fidl::FidlCodedStruct(
    multiple_nonnullable_strings_fields, ArrayCount(multiple_nonnullable_strings_fields),
    sizeof(multiple_nonnullable_strings_inline_data),
    "multiple_nonnullable_strings_message"));

static const fidl::FidlField multiple_nullable_strings_fields[] = {
    fidl::FidlField(&bounded_32_nullable_string,
                    offsetof(multiple_nullable_strings_message_layout, inline_struct.string)),

    fidl::FidlField(&bounded_32_nullable_string,
                    offsetof(multiple_nullable_strings_message_layout, inline_struct.string2)),
};
const fidl_type_t multiple_nullable_strings_message_type = fidl_type_t(fidl::FidlCodedStruct(
    multiple_nullable_strings_fields, ArrayCount(multiple_nullable_strings_fields),
    sizeof(multiple_nullable_strings_inline_data),
    "multiple_nullable_strings_message"));

static const fidl::FidlField multiple_short_nonnullable_strings_fields[] = {
    fidl::FidlField(
        &bounded_4_nonnullable_string,
        offsetof(multiple_short_nonnullable_strings_message_layout, inline_struct.string)),

    fidl::FidlField(
        &bounded_32_nonnullable_string,
        offsetof(multiple_short_nonnullable_strings_message_layout, inline_struct.string2)),
};
const fidl_type_t multiple_short_nonnullable_strings_message_type =
    fidl_type_t(fidl::FidlCodedStruct(multiple_short_nonnullable_strings_fields,
                                      ArrayCount(multiple_short_nonnullable_strings_fields),
                                      sizeof(multiple_short_nonnullable_strings_inline_data),
                                      "multiple_short_nonnullable_strings_message"));

static const fidl::FidlField multiple_short_nullable_strings_fields[] = {
    fidl::FidlField(&bounded_4_nullable_string,
                    offsetof(multiple_short_nullable_strings_message_layout, inline_struct.string)),

    fidl::FidlField(
        &bounded_32_nullable_string,
        offsetof(multiple_short_nullable_strings_message_layout, inline_struct.string2)),
};
const fidl_type_t multiple_short_nullable_strings_message_type = fidl_type_t(fidl::FidlCodedStruct(
    multiple_short_nullable_strings_fields, ArrayCount(multiple_short_nullable_strings_fields),
    sizeof(multiple_short_nullable_strings_inline_data),
    "multiple_short_nullable_strings_message"));

// Vector messages.
static const fidl::FidlField unbounded_nonnullable_vector_of_handles_fields[] = {
    fidl::FidlField(
        &unbounded_nonnullable_vector_of_handles,
        offsetof(unbounded_nonnullable_vector_of_handles_message_layout, inline_struct.vector)),

};
const fidl_type_t unbounded_nonnullable_vector_of_handles_message_type =
    fidl_type_t(fidl::FidlCodedStruct(unbounded_nonnullable_vector_of_handles_fields,
                                      ArrayCount(unbounded_nonnullable_vector_of_handles_fields),
                                      sizeof(unbounded_nonnullable_vector_of_handles_inline_data),
                                      "unbounded_nonnullable_vector_of_handles_message"));

static const fidl::FidlField unbounded_nullable_vector_of_handles_fields[] = {
    fidl::FidlField(
        &unbounded_nullable_vector_of_handles,
        offsetof(unbounded_nullable_vector_of_handles_message_layout, inline_struct.vector)),

};
const fidl_type_t unbounded_nullable_vector_of_handles_message_type =
    fidl_type_t(fidl::FidlCodedStruct(unbounded_nullable_vector_of_handles_fields,
                                      ArrayCount(unbounded_nullable_vector_of_handles_fields),
                                      sizeof(unbounded_nullable_vector_of_handles_inline_data),
                                      "unbounded_nullable_vector_of_handles_message"));

static const fidl::FidlField bounded_32_nonnullable_vector_of_handles_fields[] = {
    fidl::FidlField(
        &bounded_32_nonnullable_vector_of_handles,
        offsetof(bounded_32_nonnullable_vector_of_handles_message_layout, inline_struct.vector)),

};
const fidl_type_t bounded_32_nonnullable_vector_of_handles_message_type = fidl_type_t(
    fidl::FidlCodedStruct(bounded_32_nonnullable_vector_of_handles_fields,
                          ArrayCount(bounded_32_nonnullable_vector_of_handles_fields),
                          sizeof(bounded_32_nonnullable_vector_of_handles_inline_data),
                          "bounded_32_nonnullable_vector_of_handles_message"));

static const fidl::FidlField bounded_32_nullable_vector_of_handles_fields[] = {
    fidl::FidlField(
        &bounded_32_nullable_vector_of_handles,
        offsetof(bounded_32_nullable_vector_of_handles_message_layout, inline_struct.vector)),

};
const fidl_type_t bounded_32_nullable_vector_of_handles_message_type =
    fidl_type_t(fidl::FidlCodedStruct(bounded_32_nullable_vector_of_handles_fields,
                                      ArrayCount(bounded_32_nullable_vector_of_handles_fields),
                                      sizeof(bounded_32_nullable_vector_of_handles_inline_data),
                                      "bounded_32_nullable_vector_of_handles_message"));

static const fidl::FidlField multiple_nonnullable_vectors_of_handles_fields[] = {
    fidl::FidlField(
        &bounded_2_nonnullable_vector_of_handles,
        offsetof(multiple_nonnullable_vectors_of_handles_message_layout, inline_struct.vector)),
    fidl::FidlField(
        &unbounded_nonnullable_vector_of_handles,
        offsetof(multiple_nonnullable_vectors_of_handles_message_layout, inline_struct.vector2)),
};
const fidl_type_t multiple_nonnullable_vectors_of_handles_message_type =
    fidl_type_t(fidl::FidlCodedStruct(multiple_nonnullable_vectors_of_handles_fields,
                                      ArrayCount(multiple_nonnullable_vectors_of_handles_fields),
                                      sizeof(multiple_nonnullable_vectors_of_handles_inline_data),
                                      "multiple_nonnullable_vectors_of_handles_message"));

static const fidl::FidlField multiple_nullable_vectors_of_handles_fields[] = {
    fidl::FidlField(
        &bounded_2_nullable_vector_of_handles,
        offsetof(multiple_nullable_vectors_of_handles_message_layout, inline_struct.vector)),
    fidl::FidlField(
        &unbounded_nullable_vector_of_handles,
        offsetof(multiple_nullable_vectors_of_handles_message_layout, inline_struct.vector2)),
};
const fidl_type_t multiple_nullable_vectors_of_handles_message_type =
    fidl_type_t(fidl::FidlCodedStruct(multiple_nullable_vectors_of_handles_fields,
                                      ArrayCount(multiple_nullable_vectors_of_handles_fields),
                                      sizeof(multiple_nullable_vectors_of_handles_inline_data),
                                      "multiple_nullable_vectors_of_handles_message"));

static const fidl::FidlField unbounded_nonnullable_vector_of_uint32_fields[] = {
    fidl::FidlField(
        &unbounded_nonnullable_vector_of_uint32,
        offsetof(unbounded_nonnullable_vector_of_uint32_message_layout, inline_struct.vector)),

};
const fidl_type_t unbounded_nonnullable_vector_of_uint32_message_type =
    fidl_type_t(fidl::FidlCodedStruct(unbounded_nonnullable_vector_of_uint32_fields,
                                      ArrayCount(unbounded_nonnullable_vector_of_uint32_fields),
                                      sizeof(unbounded_nonnullable_vector_of_uint32_inline_data),
                                      "unbounded_nonnullable_vector_of_uint32_message"));

static const fidl::FidlField unbounded_nullable_vector_of_uint32_fields[] = {
    fidl::FidlField(
        &unbounded_nullable_vector_of_uint32,
        offsetof(unbounded_nullable_vector_of_uint32_message_layout, inline_struct.vector)),

};
const fidl_type_t unbounded_nullable_vector_of_uint32_message_type =
    fidl_type_t(fidl::FidlCodedStruct(unbounded_nullable_vector_of_uint32_fields,
                                      ArrayCount(unbounded_nullable_vector_of_uint32_fields),
                                      sizeof(unbounded_nullable_vector_of_uint32_inline_data),
                                      "unbounded_nullable_vector_of_uint32_message"));

static const fidl::FidlField bounded_32_nonnullable_vector_of_uint32_fields[] = {
    fidl::FidlField(
        &bounded_32_nonnullable_vector_of_uint32,
        offsetof(bounded_32_nonnullable_vector_of_uint32_message_layout, inline_struct.vector)),

};
const fidl_type_t bounded_32_nonnullable_vector_of_uint32_message_type = fidl_type_t(
    fidl::FidlCodedStruct(bounded_32_nonnullable_vector_of_uint32_fields,
                          ArrayCount(bounded_32_nonnullable_vector_of_uint32_fields),
                          sizeof(bounded_32_nonnullable_vector_of_uint32_inline_data),
                          "bounded_32_nonnullable_vector_of_uint32_message"));

static const fidl::FidlField bounded_32_nullable_vector_of_uint32_fields[] = {
    fidl::FidlField(
        &bounded_32_nullable_vector_of_uint32,
        offsetof(bounded_32_nullable_vector_of_uint32_message_layout, inline_struct.vector)),

};
const fidl_type_t bounded_32_nullable_vector_of_uint32_message_type =
    fidl_type_t(fidl::FidlCodedStruct(bounded_32_nullable_vector_of_uint32_fields,
                                      ArrayCount(bounded_32_nullable_vector_of_uint32_fields),
                                      sizeof(bounded_32_nullable_vector_of_uint32_inline_data),
                                      "bounded_32_nullable_vector_of_uint32_message"));

static const fidl::FidlField multiple_nonnullable_vectors_of_uint32_fields[] = {
    fidl::FidlField(
        &bounded_2_nonnullable_vector_of_uint32,
        offsetof(multiple_nonnullable_vectors_of_uint32_message_layout, inline_struct.vector)),
    fidl::FidlField(
        &unbounded_nonnullable_vector_of_uint32,
        offsetof(multiple_nonnullable_vectors_of_uint32_message_layout, inline_struct.vector2)),
};
const fidl_type_t multiple_nonnullable_vectors_of_uint32_message_type =
    fidl_type_t(fidl::FidlCodedStruct(multiple_nonnullable_vectors_of_uint32_fields,
                                      ArrayCount(multiple_nonnullable_vectors_of_uint32_fields),
                                      sizeof(multiple_nonnullable_vectors_of_uint32_inline_data),
                                      "multiple_nonnullable_vectors_of_uint32_message"));

static const fidl::FidlField multiple_nullable_vectors_of_uint32_fields[] = {
    fidl::FidlField(
        &bounded_2_nullable_vector_of_uint32,
        offsetof(multiple_nullable_vectors_of_uint32_message_layout, inline_struct.vector)),
    fidl::FidlField(
        &unbounded_nullable_vector_of_uint32,
        offsetof(multiple_nullable_vectors_of_uint32_message_layout, inline_struct.vector2)),
};
const fidl_type_t multiple_nullable_vectors_of_uint32_message_type =
    fidl_type_t(fidl::FidlCodedStruct(multiple_nullable_vectors_of_uint32_fields,
                                      ArrayCount(multiple_nullable_vectors_of_uint32_fields),
                                      sizeof(multiple_nullable_vectors_of_uint32_inline_data),
                                      "multiple_nullable_vectors_of_uint32_message"));

// Union messages.
static const fidl_type_t* nonnullable_handle_union_members[] = {
    &nonnullable_handle,
};
const fidl_type_t nonnullable_handle_union_type = fidl_type_t(fidl::FidlCodedUnion(
    nonnullable_handle_union_members,
    ArrayCount(nonnullable_handle_union_members),
    offsetof(nonnullable_handle_union, handle),
    sizeof(nonnullable_handle_union),
    "nonnullable_handle_union"));
static const fidl::FidlField nonnullable_handle_union_fields[] = {
    fidl::FidlField(&nonnullable_handle_union_type,
                    offsetof(nonnullable_handle_union_message_layout, inline_struct.data)),
};
const fidl_type_t nonnullable_handle_union_message_type = fidl_type_t(fidl::FidlCodedStruct(
    nonnullable_handle_union_fields, ArrayCount(nonnullable_handle_union_fields),
    sizeof(nonnullable_handle_union_inline_data),
    "nonnullable_handle_union_message"));

static const fidl_type_t* array_of_nonnullable_handles_union_members[] = {
    &nonnullable_handle,
    &array_of_two_nonnullable_handles,
    &array_of_two_arrays_of_two_nonnullable_handles,
};
static const fidl_type_t array_of_nonnullable_handles_union_type = fidl_type_t(
    fidl::FidlCodedUnion(array_of_nonnullable_handles_union_members,
                         ArrayCount(array_of_nonnullable_handles_union_members),
                         offsetof(array_of_nonnullable_handles_union, handle),
                         sizeof(array_of_nonnullable_handles_union),
                         "array_of_nonnullable_handles_union"));
static const fidl::FidlField array_of_nonnullable_handles_union_fields[] = {
    fidl::FidlField(
        &array_of_nonnullable_handles_union_type,
        offsetof(array_of_nonnullable_handles_union_message_layout, inline_struct.data)),

};
const fidl_type_t array_of_nonnullable_handles_union_message_type =
    fidl_type_t(fidl::FidlCodedStruct(array_of_nonnullable_handles_union_fields,
                                      ArrayCount(array_of_nonnullable_handles_union_fields),
                                      sizeof(array_of_nonnullable_handles_union_inline_data),
                                      "array_of_nonnullable_handles_union_message"));

// Union pointer messages.
const fidl_type_t nonnullable_handle_union_ptr =
    fidl_type_t(fidl::FidlCodedUnionPointer(&nonnullable_handle_union_type.coded_union));
static const fidl::FidlField nonnullable_handle_union_ptr_fields[] = {
    fidl::FidlField(&nonnullable_handle_union_ptr,
                    offsetof(nonnullable_handle_union_ptr_inline_data, data)),
};
const fidl_type_t nonnullable_handle_union_ptr_message_type = fidl_type_t(fidl::FidlCodedStruct(
    nonnullable_handle_union_ptr_fields, ArrayCount(nonnullable_handle_union_ptr_fields),
    sizeof(nonnullable_handle_union_ptr_inline_data),
    "nonnullable_handle_union_ptr_message"));

static const fidl_type_t array_of_nonnullable_handles_union_ptr =
    fidl_type_t(fidl::FidlCodedUnionPointer(&array_of_nonnullable_handles_union_type.coded_union));
static const fidl::FidlField array_of_nonnullable_handles_union_ptr_fields[] = {
    fidl::FidlField(&array_of_nonnullable_handles_union_ptr,
                    offsetof(array_of_nonnullable_handles_union_ptr_inline_data, data)),
};
const fidl_type_t array_of_nonnullable_handles_union_ptr_message_type =
    fidl_type_t(fidl::FidlCodedStruct(array_of_nonnullable_handles_union_ptr_fields,
                                      ArrayCount(array_of_nonnullable_handles_union_ptr_fields),
                                      sizeof(array_of_nonnullable_handles_union_ptr_inline_data),
                                      "array_of_nonnullable_handles_union_ptr_message"));

// Struct messages.
static const fidl::FidlField struct_level_3_fields[] = {
    fidl::FidlField(&nonnullable_handle, offsetof(struct_level_3, handle_3)),
};
static const fidl_type_t struct_level_3_struct = fidl_type_t(fidl::FidlCodedStruct(
    struct_level_3_fields, ArrayCount(struct_level_3_fields), sizeof(struct_level_3),
    "struct_level_3"));
static const fidl::FidlField struct_level_2_fields[] = {
    fidl::FidlField(&struct_level_3_struct, offsetof(struct_level_2, l3)),
    fidl::FidlField(&nonnullable_handle, offsetof(struct_level_2, handle_2)),
};
static const fidl_type_t struct_level_2_struct = fidl_type_t(fidl::FidlCodedStruct(
    struct_level_2_fields, ArrayCount(struct_level_2_fields), sizeof(struct_level_2),
    "struct_level_2"));
static const fidl::FidlField struct_level_1_fields[] = {
    fidl::FidlField(&nonnullable_handle, offsetof(struct_level_1, handle_1)),
    fidl::FidlField(&struct_level_2_struct, offsetof(struct_level_1, l2)),
};
static const fidl_type_t struct_level_1_struct = fidl_type_t(fidl::FidlCodedStruct(
    struct_level_1_fields, ArrayCount(struct_level_1_fields), sizeof(struct_level_1),
    "struct_level_1"));
static const fidl::FidlField struct_level_0_fields[] = {
    fidl::FidlField(&struct_level_1_struct, offsetof(struct_level_0, l1)),
    fidl::FidlField(&nonnullable_handle, offsetof(struct_level_0, handle_0)),
};
const fidl_type_t struct_level_0_struct = fidl_type_t(fidl::FidlCodedStruct(
    struct_level_0_fields, ArrayCount(struct_level_1_fields), sizeof(struct_level_0),
    "struct_level_0"));
static const fidl::FidlField nested_structs_fields[] = {
    fidl::FidlField(&struct_level_0_struct,
                    offsetof(nested_structs_message_layout, inline_struct.l0)),

};
const fidl_type_t nested_structs_message_type = fidl_type_t(fidl::FidlCodedStruct(
    nested_structs_fields, ArrayCount(nested_structs_fields), sizeof(nested_structs_inline_data),
    "nested_structs_message"));

// Struct pointer messages.
static const fidl::FidlField struct_ptr_level_3_fields[] = {
    fidl::FidlField(&nonnullable_handle, offsetof(struct_ptr_level_3, handle_3)),

};
static const fidl_type_t struct_ptr_level_3_struct = fidl_type_t(fidl::FidlCodedStruct(
    struct_ptr_level_3_fields, ArrayCount(struct_ptr_level_3_fields), sizeof(struct_ptr_level_3),
    "struct_ptr_level_3"));
static const fidl_type_t struct_ptr_level_3_struct_pointer =
    fidl_type_t(fidl::FidlCodedStructPointer(&struct_ptr_level_3_struct.coded_struct));
static const fidl::FidlField struct_ptr_level_2_fields[] = {
    fidl::FidlField(&struct_ptr_level_3_struct_pointer, offsetof(struct_ptr_level_2, l3_present)),
    fidl::FidlField(&struct_ptr_level_3_struct_pointer, offsetof(struct_ptr_level_2, l3_absent)),
    fidl::FidlField(&struct_ptr_level_3_struct, offsetof(struct_ptr_level_2, l3_inline)),
    fidl::FidlField(&nonnullable_handle, offsetof(struct_ptr_level_2, handle_2)),
};
static const fidl_type_t struct_ptr_level_2_struct = fidl_type_t(fidl::FidlCodedStruct(
    struct_ptr_level_2_fields, ArrayCount(struct_ptr_level_2_fields), sizeof(struct_ptr_level_2),
    "struct_ptr_level_2"));
static const fidl_type_t struct_ptr_level_2_struct_pointer =
    fidl_type_t(fidl::FidlCodedStructPointer(&struct_ptr_level_2_struct.coded_struct));
static const fidl::FidlField struct_ptr_level_1_fields[] = {
    fidl::FidlField(&nonnullable_handle, offsetof(struct_ptr_level_1, handle_1)),
    fidl::FidlField(&struct_ptr_level_2_struct_pointer, offsetof(struct_ptr_level_1, l2_present)),
    fidl::FidlField(&struct_ptr_level_2_struct, offsetof(struct_ptr_level_1, l2_inline)),
    fidl::FidlField(&struct_ptr_level_2_struct_pointer, offsetof(struct_ptr_level_1, l2_absent)),
};
static const fidl_type_t struct_ptr_level_1_struct = fidl_type_t(fidl::FidlCodedStruct(
    struct_ptr_level_1_fields, ArrayCount(struct_ptr_level_1_fields), sizeof(struct_ptr_level_1),
    "struct_ptr_level_1"));
static const fidl_type_t struct_ptr_level_1_struct_pointer =
    fidl_type_t(fidl::FidlCodedStructPointer(&struct_ptr_level_1_struct.coded_struct));
static const fidl::FidlField struct_ptr_level_0_fields[] = {
    fidl::FidlField(&struct_ptr_level_1_struct_pointer, offsetof(struct_ptr_level_0, l1_absent)),
    fidl::FidlField(&struct_ptr_level_1_struct, offsetof(struct_ptr_level_0, l1_inline)),
    fidl::FidlField(&nonnullable_handle, offsetof(struct_ptr_level_0, handle_0)),
    fidl::FidlField(&struct_ptr_level_1_struct_pointer, offsetof(struct_ptr_level_0, l1_present)),
};
static const fidl_type_t struct_ptr_level_0_struct = fidl_type_t(fidl::FidlCodedStruct(
    struct_ptr_level_0_fields, ArrayCount(struct_ptr_level_0_fields), sizeof(struct_ptr_level_0),
    "struct_ptr_level_0"));
const fidl_type_t struct_ptr_level_0_struct_pointer =
    fidl_type_t(fidl::FidlCodedStructPointer(&struct_ptr_level_0_struct.coded_struct));
static const fidl::FidlField nested_struct_ptrs_fields[] = {
    fidl::FidlField(&struct_ptr_level_0_struct,
                    offsetof(nested_struct_ptrs_inline_data, l0_inline)),
    fidl::FidlField(&struct_ptr_level_0_struct_pointer,
                    offsetof(nested_struct_ptrs_inline_data, l0_absent)),
    fidl::FidlField(&struct_ptr_level_0_struct_pointer,
                    offsetof(nested_struct_ptrs_inline_data, l0_present)),
};
const fidl_type_t nested_struct_ptrs_message_type = fidl_type_t(
    fidl::FidlCodedStruct(nested_struct_ptrs_fields, ArrayCount(nested_struct_ptrs_fields),
                          sizeof(nested_struct_ptrs_inline_data),
                          "nested_struct_ptrs_message"));

// Recursive struct pointer messages.
const fidl_type_t recursion_message_ptr_type = fidl_type_t(fidl::FidlCodedStructPointer(
    &recursion_message_type.coded_struct));
static const fidl_type_t* maybe_recurse_union_members[] = {
    &nonnullable_handle,
    &recursion_message_ptr_type,
};
const fidl_type_t maybe_recurse_type = fidl_type_t(fidl::FidlCodedUnion(
    maybe_recurse_union_members, ArrayCount(maybe_recurse_union_members),
    offsetof(maybe_recurse, handle), sizeof(maybe_recurse), "maybe_recurse"));
static const fidl::FidlField recursion_fields[] = {
    fidl::FidlField(&maybe_recurse_type, offsetof(recursion_inline_data, inline_union)),
};
const fidl_type_t recursion_message_type = fidl_type_t(fidl::FidlCodedStruct(
    recursion_fields, ArrayCount(recursion_fields), sizeof(recursion_inline_data),
    "recursion_message"));
