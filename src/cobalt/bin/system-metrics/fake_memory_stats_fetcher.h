// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_COBALT_BIN_SYSTEM_METRICS_FAKE_MEMORY_STATS_FETCHER_H_
#define SRC_COBALT_BIN_SYSTEM_METRICS_FAKE_MEMORY_STATS_FETCHER_H_

#include <lib/zx/resource.h>

#include "src/cobalt/bin/system-metrics/memory_stats_fetcher.h"

namespace cobalt {

class FakeMemoryStatsFetcher : public cobalt::MemoryStatsFetcher {
 public:
  FakeMemoryStatsFetcher();
  bool FetchMemoryStats(zx_info_kmem_stats_t* mem_stats) override;
};

}  // namespace cobalt

#endif  // SRC_COBALT_BIN_SYSTEM_METRICS_FAKE_MEMORY_STATS_FETCHER_H_