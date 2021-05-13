// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZBITL_ITEMS_INTERNAL_MEM_RANGE_TYPES_H_
#define LIB_ZBITL_ITEMS_INTERNAL_MEM_RANGE_TYPES_H_

#include <lib/stdcompat/span.h>
#include <lib/zbitl/storage_traits.h>
#include <zircon/boot/image.h>
#include <zircon/types.h>

struct e820entry;

namespace zbitl::internal {

// A view into a ZBI_TYPE_MEM_CONFIG table.
struct MemConfigTable {
  cpp20::span<const zbi_mem_range_t> table;
};

// A view into a ZBI_TYPE_E820_TABLE table.
struct E820Table {
  cpp20::span<const struct e820entry> table;
};

// A view into a ZBI_TYPE_EFI_MEMORY_MAP table.
struct EfiTable {
  size_t num_entries;
  size_t entry_size;
  ByteView payload;
};

}  // namespace zbitl::internal

#endif  // LIB_ZBITL_ITEMS_INTERNAL_MEM_RANGE_TYPES_H_
