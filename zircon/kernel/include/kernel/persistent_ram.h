// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_INCLUDE_KERNEL_PERSISTENT_RAM_H_
#define ZIRCON_KERNEL_INCLUDE_KERNEL_PERSISTENT_RAM_H_

#include <pow2.h>

#ifndef PERSISTENT_RAM_ALLOCATION_GRANULARITY
#define PERSISTENT_RAM_ALLOCATION_GRANULARITY 128
#endif

static constexpr size_t kPersistentRamAllocationGranularity = PERSISTENT_RAM_ALLOCATION_GRANULARITY;

static_assert(ispow2(kPersistentRamAllocationGranularity) &&
                  (kPersistentRamAllocationGranularity > 0),
              "The allocation granularity of persistent RAM must be a power of two greater than 0");

#endif  // ZIRCON_KERNEL_INCLUDE_KERNEL_PERSISTENT_RAM_H_
