// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_ACPI_MEMORY_REGION_UTIL_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_ACPI_MEMORY_REGION_UTIL_H_

#include <lib/zircon-internal/align.h>
#include <zircon/assert.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <cstddef>
#include <limits>
#include <utility>

namespace i915_tgl {

template <typename T>
std::pair<T, size_t> RoundToPageBoundaries(T region_start_address, size_t region_size) {
  ZX_ASSERT(region_size > 0);
  ZX_ASSERT(region_size < std::numeric_limits<T>::max());
  ZX_ASSERT(std::numeric_limits<T>::max() - region_size >= region_start_address);

  const T page_bits_mask = T{zx_system_get_page_size() - 1};
  ZX_ASSERT_MSG((page_bits_mask & (page_bits_mask + 1)) == 0,
                "zx_system_get_page_size() is not a power of two");

  const T first_page_address = region_start_address & ~page_bits_mask;
  ZX_ASSERT(first_page_address <= region_start_address);

  const uint32_t page_offset = static_cast<uint32_t>(region_start_address & page_bits_mask);
  ZX_ASSERT(page_offset == region_start_address - first_page_address);

  const size_t page_region_size = ZX_PAGE_ALIGN(region_size + page_offset);

  ZX_ASSERT(first_page_address + page_region_size >= region_start_address + region_size);
  return {first_page_address, page_region_size};
}

}  // namespace i915_tgl

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_ACPI_MEMORY_REGION_UTIL_H_
