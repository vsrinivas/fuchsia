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

static const FidlCodedString unbounded_nonnullable_string = {
    .tag = kFidlTypeString,
    .nullable = kFidlNullability_Nonnullable,
    .max_size = FIDL_MAX_SIZE,
};

struct unbounded_nonnullable_string_inline_data {
  fidl_message_header_t header;
  fidl_string_t string;
};

struct unbounded_nonnullable_string_message_layout {
  unbounded_nonnullable_string_inline_data inline_struct;
  alignas(FIDL_ALIGNMENT) char data[6];
};

static const FidlStructElement unbounded_nonnullable_string_fields[] = {
    FidlStructElement::Field(
        &unbounded_nonnullable_string,
        offsetof(unbounded_nonnullable_string_message_layout, inline_struct.string),
        kFidlIsResource_NotResource),
};

const FidlCodedStruct unbounded_nonnullable_string_message_type = {
    .tag = kFidlTypeStruct,
    .element_count = ArrayCount(unbounded_nonnullable_string_fields),
    .size = sizeof(unbounded_nonnullable_string_inline_data),
    .elements = unbounded_nonnullable_string_fields,
    .name = "unbounded_nonnullable_string_message",
};

const FidlCodedStruct zero_arg_message_type = {
    .tag = kFidlTypeStruct,
    .element_count = 0,
    .size = sizeof(fidl_message_header_t),
    .elements = nullptr,
    .name = "zero_arg_message_type",
};
