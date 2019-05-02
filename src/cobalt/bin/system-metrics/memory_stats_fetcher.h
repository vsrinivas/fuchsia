// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_COBALT_BIN_SYSTEM_METRICS_MEMORY_STATS_FETCHER_H_
#define SRC_COBALT_BIN_SYSTEM_METRICS_MEMORY_STATS_FETCHER_H_

#include <lib/zx/resource.h>

namespace cobalt {

// An abstrace interface to a
class MemoryStatsFetcher {
 public:
  virtual ~MemoryStatsFetcher() = default;

  virtual bool FetchMemoryStats(zx_info_kmem_stats_t* mem_stats) = 0;
};

}  // namespace cobalt

#endif  // SRC_COBALT_BIN_SYSTEM_METRICS_MEMORY_STATS_FETCHER_H_
