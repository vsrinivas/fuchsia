// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/test/fidl_types.h"

#include <lib/fidl/internal.h>

#include <cstdint>
#include <limits>

// All sizes in fidl encoding tables are 32 bits. The fidl compiler
// normally enforces this. Check manually in manual tests.
template <typename T, size_t N>
constexpr uint32_t ArrayCount(T const (&array)[N]) {
  static_assert(N <= std::numeric_limits<uint32_t>::max(), "Array is too large!");
  return static_cast<uint32_t>(N);
}

static const fidl_type_t unbounded_nonnullable_string = {
    .type_tag = kFidlTypeString,
    {.coded_string = {.max_size = FIDL_MAX_SIZE, .nullable = kFidlNullability_Nonnullable}}};

struct unbounded_nonnullable_string_inline_data {
  fidl_message_header_t header;
  fidl_string_t string;
};

struct unbounded_nonnullable_string_message_layout {
  unbounded_nonnullable_string_inline_data inline_struct;
  alignas(FIDL_ALIGNMENT) char data[6];
};

static const FidlStructField unbounded_nonnullable_string_fields[] = {
    FidlStructField(&unbounded_nonnullable_string,
                    offsetof(unbounded_nonnullable_string_message_layout, inline_struct.string), 0),
};

const FidlCodedStruct unbounded_nonnullable_string_message{
    .fields = unbounded_nonnullable_string_fields,
    .field_count = ArrayCount(unbounded_nonnullable_string_fields),
    .size = sizeof(unbounded_nonnullable_string_inline_data),
    .max_out_of_line = 0xFFFFFFFF,
    .contains_union = false,
    .name = "unbounded_nonnullable_string_message",
    .alt_type = &unbounded_nonnullable_string_message};

const fidl_type_t unbounded_nonnullable_string_message_type = {
    .type_tag = kFidlTypeStruct, {.coded_struct = unbounded_nonnullable_string_message}};
