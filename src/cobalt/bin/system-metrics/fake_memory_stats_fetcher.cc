// Copyright 2019  The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/system-metrics/fake_memory_stats_fetcher.h"

#include <lib/zx/resource.h>

namespace cobalt {

FakeMemoryStatsFetcher::FakeMemoryStatsFetcher() {}

bool FakeMemoryStatsFetcher::FetchMemoryStats(zx_info_kmem_stats_t* mem_stats) {
  mem_stats->total_bytes = 100;
  mem_stats->free_bytes = 40;
  mem_stats->wired_bytes = 10;
  mem_stats->total_heap_bytes = 20;
  mem_stats->free_heap_bytes = 5;
  mem_stats->vmo_bytes = 10;
  mem_stats->mmu_overhead_bytes = 6;
  mem_stats->other_bytes = 9;
  return true;
}

}  // namespace cobalt