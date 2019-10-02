// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_COBALT_BIN_SYSTEM_METRICS_TESTING_FAKE_MEMORY_STATS_FETCHER_H_
#define SRC_COBALT_BIN_SYSTEM_METRICS_TESTING_FAKE_MEMORY_STATS_FETCHER_H_

#include <lib/zx/resource.h>

#include "src/cobalt/bin/system-metrics/memory_stats_fetcher.h"

namespace cobalt {

class FakeMemoryStatsFetcher : public cobalt::MemoryStatsFetcher {
 public:
  FakeMemoryStatsFetcher();
  bool FetchMemoryStats(llcpp::fuchsia::kernel::MemoryStats** mem_stats) override;
 private:
  llcpp::fuchsia::kernel::MemoryStats mem_stats_;
  llcpp::fuchsia::kernel::MemoryStats::Builder builder_;
  uint64_t total_bytes_ = 100;
  uint64_t free_bytes_ = 40;
  uint64_t wired_bytes_ = 10;
  uint64_t heap_bytes_ = 20;
  uint64_t free_heap_bytes_ = 15;
  uint64_t vmo_bytes_ = 10;
  uint64_t mmu_overhead_bytes_ = 6;
  uint64_t other_bytes_ = 9;
  uint64_t ipc_bytes_ = 0;
};

}  // namespace cobalt

#endif  // SRC_COBALT_BIN_SYSTEM_METRICS_TESTING_FAKE_MEMORY_STATS_FETCHER_H_
