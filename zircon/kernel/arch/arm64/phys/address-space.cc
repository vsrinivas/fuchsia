// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <phys/arch.h>

void ArchSetUpAddressSpace(memalloc::Allocator& allocator, const zbitl::MemRangeTable& table) {
  // TODO(fxb/67632): Implement support for creating an ARM page table,
  // allowing us to run using CPU caches enabled.
}
