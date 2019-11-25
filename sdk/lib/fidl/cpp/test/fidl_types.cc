// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/test/fidl_types.h"

#include <lib/fidl/internal.h>
#include <stdint.h>

// All sizes in fidl encoding tables are 32 bits. The fidl compiler
// normally enforces this. Check manually in manual tests.
template <typename T, size_t N>
static uint32_t ArrayCount(T const (&array)[N]) {
  static_assert(N < UINT32_MAX, "Array is too large!");
  return N;
}

static const fidl_type_t unbounded_nonnullable_string =
    fidl_type_t(fidl::FidlCodedString(FIDL_MAX_SIZE, fidl::kNonnullable));

struct unbounded_nonnullable_string_inline_data {
  fidl_message_header_t header;
  fidl_string_t string;
};

struct unbounded_nonnullable_string_message_layout {
  unbounded_nonnullable_string_inline_data inline_struct;
  alignas(FIDL_ALIGNMENT) char data[6];
};

static const fidl::FidlStructField unbounded_nonnullable_string_fields[] = {
    fidl::FidlStructField(
        &unbounded_nonnullable_string,
        offsetof(unbounded_nonnullable_string_message_layout, inline_struct.string), 0),

};

const fidl::FidlCodedStruct unbounded_nonnullable_string_message = fidl::FidlCodedStruct(
    unbounded_nonnullable_string_fields, ArrayCount(unbounded_nonnullable_string_fields),
    sizeof(unbounded_nonnullable_string_inline_data), 0xFFFFFFFF, false,
    "unbounded_nonnullable_string_message", &unbounded_nonnullable_string_message);

const fidl_type_t unbounded_nonnullable_string_message_type =
    fidl_type_t(unbounded_nonnullable_string_message);
