// Copyright 2019  The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/system-metrics/testing/fake_memory_stats_fetcher.h"

#include <stdio.h>

namespace cobalt {

FakeMemoryStatsFetcher::FakeMemoryStatsFetcher()
    : builder_(llcpp::fuchsia::kernel::MemoryStats::Build()) {}

bool FakeMemoryStatsFetcher::FetchMemoryStats(llcpp::fuchsia::kernel::MemoryStats** mem_stats) {
  builder_.set_total_bytes(&total_bytes_);
  builder_.set_free_bytes(&free_bytes_);
  builder_.set_wired_bytes(&wired_bytes_);
  builder_.set_total_heap_bytes(&heap_bytes_);
  builder_.set_free_heap_bytes(&free_heap_bytes_);
  builder_.set_vmo_bytes(&vmo_bytes_);
  builder_.set_mmu_overhead_bytes(&mmu_overhead_bytes_);
  builder_.set_other_bytes(&other_bytes_);
  builder_.set_ipc_bytes(&ipc_bytes_);
  mem_stats_ = builder_.view();
  *mem_stats = &mem_stats_;
  return true;
}

}  // namespace cobalt
