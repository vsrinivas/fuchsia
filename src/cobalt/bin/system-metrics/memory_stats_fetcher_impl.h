// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_COBALT_BIN_SYSTEM_METRICS_MEMORY_STATS_FETCHER_IMPL_H_
#define SRC_COBALT_BIN_SYSTEM_METRICS_MEMORY_STATS_FETCHER_IMPL_H_

#include <lib/zx/resource.h>
#include "src/cobalt/bin/system-metrics/memory_stats_fetcher.h"

using cobalt::MemoryStatsFetcher;

namespace cobalt {

class MemoryStatsFetcherImpl : public MemoryStatsFetcher {
 public:
  MemoryStatsFetcherImpl();

  bool FetchMemoryStats(zx_info_kmem_stats_t* mem_stats) override;

 private:
  void InitializeRootResourceHandle();

  zx_handle_t root_resource_handle_ = ZX_HANDLE_INVALID;
};

}  // namespace cobalt

#endif  // SRC_COBALT_BIN_SYSTEM_METRICS_MEMORY_STATS_FETCHER_IMPL_H_
