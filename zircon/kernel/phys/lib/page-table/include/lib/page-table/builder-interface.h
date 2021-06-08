// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_PHYS_LIB_PAGE_TABLE_INCLUDE_LIB_PAGE_TABLE_BUILDER_INTERFACE_H_
#define ZIRCON_KERNEL_PHYS_LIB_PAGE_TABLE_INCLUDE_LIB_PAGE_TABLE_BUILDER_INTERFACE_H_

#include <lib/page-table/types.h>
#include <zircon/types.h>

#include <cstddef>

namespace page_table {

// Convenience class for building address spaces.
//
// See <page-table/builder.h> for the concrete instantiation for the current architecture.
class AddressSpaceBuilderInterface {
 public:
  // Map `size` bytes starting from `virt_start` to `phys_start`.
  //
  // Existing mappings will be overwritten.
  //
  // Input addresses and size must be aligned to at least the smallest
  // support page size on the architecture. `MapRegion` will attempt to use
  // larger pages where possible (e.g., when both `virt_start` and
  // `phys_start` are similarly aligned).
  //
  // Returns errors if input arguments are invalid.
  virtual zx_status_t MapRegion(Vaddr virt_start, Paddr phys_start, uint64_t size,
                                CacheAttributes cache_attrs) = 0;

  // Return the Paddr of the root node of the translation table.
  virtual Paddr root_paddr() = 0;
};

}  // namespace page_table

#endif  // ZIRCON_KERNEL_PHYS_LIB_PAGE_TABLE_INCLUDE_LIB_PAGE_TABLE_BUILDER_INTERFACE_H_
