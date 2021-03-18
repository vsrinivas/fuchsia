// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_PHYS_INCLUDE_PHYS_ZBITL_ALLOCATION_H_
#define ZIRCON_KERNEL_PHYS_INCLUDE_PHYS_ZBITL_ALLOCATION_H_

#include <lib/fitx/result.h>

#include <ktl/string_view.h>
#include <phys/allocation.h>
#include <phys/zbitl-allocation.h>

// This matches the zbitl::View::CopyStorageItem allocator signature.
fitx::result<ktl::string_view, Allocation> ZbitlScratchAllocator(size_t size);

#endif  // ZIRCON_KERNEL_PHYS_INCLUDE_PHYS_ZBITL_ALLOCATION_H_
