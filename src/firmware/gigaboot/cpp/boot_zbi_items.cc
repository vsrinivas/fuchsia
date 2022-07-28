// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zbi/zbi.h>
#include <stdio.h>
#include <stdlib.h>
#include <zircon/limits.h>

#include <fbl/vector.h>

#include "utils.h"

namespace gigaboot {
namespace {

uint8_t scratch_buffer[32 * 1024];

bool AddMemoryRanges(void* zbi, size_t capacity) {
  uint32_t dversion = 0;
  size_t mkey = 0;
  size_t dsize = 0;
  size_t msize = sizeof(scratch_buffer);
  efi_status status = gEfiSystemTable->BootServices->GetMemoryMap(
      &msize, reinterpret_cast<efi_memory_descriptor*>(scratch_buffer), &mkey, &dsize, &dversion);
  if (status != EFI_SUCCESS) {
    printf("boot: cannot GetMemoryMap(). %s\n", EfiStatusToString(status));
    return false;
  }

  // Convert the memory map in place to a range of zbi_mem_range_t, the
  // preferred ZBI memory format. In-place conversion can safely be done
  // one-by-one, given that zbi_mem_range_t is smaller than a descriptor.
  static_assert(sizeof(zbi_mem_range_t) <= sizeof(efi_memory_descriptor),
                "Cannot assume that sizeof(zbi_mem_range_t) <= dsize");
  size_t num_ranges = msize / dsize;
  zbi_mem_range_t* ranges = reinterpret_cast<zbi_mem_range_t*>(scratch_buffer);
  for (size_t i = 0; i < num_ranges; ++i) {
    const efi_memory_descriptor* desc =
        reinterpret_cast<efi_memory_descriptor*>(scratch_buffer + i * dsize);
    const zbi_mem_range_t range = {
        .paddr = desc->PhysicalStart,
        .length = desc->NumberOfPages * kUefiPageSize,
        .type = EfiToZbiMemRangeType(desc->Type),
    };
    memcpy(&ranges[i], &range, sizeof(range));
  }

  // TODO(b/236039205): Add memory ranges for uart peripheral. Refer to
  // `src/firmware/gigaboot/src/zircon.c` at line 477.

  // TODO(b/236039205): Add memory ranges for GIC. Rfer to `src/firmware/gigaboot/src/zircon.c` at
  // line 488.

  zbi_result_t result = zbi_create_entry_with_payload(zbi, capacity, ZBI_TYPE_MEM_CONFIG, 0, 0,
                                                      ranges, num_ranges * sizeof(zbi_mem_range_t));
  if (result != ZBI_RESULT_OK) {
    printf("Failed to create entry, %d\n", result);
    return false;
  }

  return true;
}

}  // namespace

bool AddGigabootZbiItems(zbi_header_t* image, size_t capacity) {
  if (!AddMemoryRanges(image, capacity)) {
    return false;
  }

  return true;
}

}  // namespace gigaboot
