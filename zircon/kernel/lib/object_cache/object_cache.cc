// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "lib/object_cache.h"

#include <lib/counters.h>

#include <kernel/percpu.h>

KCOUNTER(cache_object_allocated, "cache.object.allocated")
KCOUNTER(cache_slab_allocated, "cache.slab.allocated")
KCOUNTER(cache_object_freed, "cache.object.freed")
KCOUNTER(cache_slab_freed, "cache.slab.freed")

namespace object_cache::internal {

size_t GetProcessorCount() { return percpu::processor_count(); }

}  // namespace object_cache::internal

namespace object_cache {

void DefaultAllocator::CountObjectAllocation() { cache_object_allocated.Add(1); }
void DefaultAllocator::CountSlabAllocation() { cache_slab_allocated.Add(1); }
void DefaultAllocator::CountObjectFree() { cache_object_freed.Add(1); }
void DefaultAllocator::CountSlabFree() { cache_slab_freed.Add(1); }

}  // namespace object_cache
