// Copyright 2019  The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/system-metrics/memory_stats_fetcher_impl.h"

#include <fcntl.h>
#include <fuchsia/cobalt/cpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fdio.h>
#include <lib/zx/resource.h>
#include <trace/event.h>
#include <zircon/status.h>

#include "lib/syslog/cpp/logger.h"

namespace cobalt {

MemoryStatsFetcherImpl::MemoryStatsFetcherImpl() { InitializeKernelStats(); }

bool MemoryStatsFetcherImpl::FetchMemoryStats(llcpp::fuchsia::kernel::MemoryStats** mem_stats) {
  TRACE_DURATION("system_metrics", "MemoryStatsFetcherImpl::FetchMemoryStats");
  if (stats_ == nullptr) {
    FX_LOGS(ERROR) << "MemoryStatsFetcherImpl: No kernel stats service"
                   << "present. Reconnecting...";
    InitializeKernelStats();
    return false;
  }
  auto result = stats_->GetMemoryStats(mem_stats_buffer_.view());
  if (result.status() != ZX_OK) {
    FX_LOGS(ERROR) << "MemoryStatsFetcherImpl: Fetching "
                   << "MemoryStats through fuchsia.kernel.Stats returns "
                   << zx_status_get_string(result.status());
    return false;
  }
  *mem_stats = &result->stats;
  return true;
}

// TODO(CF-691) When Component Stats (CS) supports memory metrics,
// switch to Component Stats / iquery, by creating a new class with the
// interface MemoryStatsFetcher.
void MemoryStatsFetcherImpl::InitializeKernelStats() {
  zx::channel local, remote;
  zx_status_t status = zx::channel::create(0, &local, &remote);
  if (status != ZX_OK) {
    return;
  }
  static const char kKernelStatsSvc[] = "/svc/fuchsia.kernel.Stats";
  status = fdio_service_connect(kKernelStatsSvc, remote.release());
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Cobalt SystemMetricsDaemon: Error getting kernel stats. "
                   << "Cannot open fuchsia.kernel.Stats: " << zx_status_get_string(status);
    return;
  }
  stats_ = std::make_unique<llcpp::fuchsia::kernel::Stats::SyncClient>(std::move(local));
}

}  // namespace cobalt
