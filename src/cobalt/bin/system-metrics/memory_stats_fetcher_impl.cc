// Copyright 2019  The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/system-metrics/memory_stats_fetcher_impl.h"

#include <fcntl.h>
#include <fuchsia/cobalt/cpp/fidl.h>
#include <fuchsia/sysinfo/c/fidl.h>
#include <lib/fdio/fdio.h>
#include <lib/fxl/logging.h>
#include <lib/zx/resource.h>
#include <zircon/status.h>

namespace cobalt {

MemoryStatsFetcherImpl::MemoryStatsFetcherImpl() {
  InitializeRootResourceHandle();
}

bool MemoryStatsFetcherImpl::FetchMemoryStats(zx_info_kmem_stats_t* mem_stats) {
  if (root_resource_handle_ == ZX_HANDLE_INVALID) {
    FXL_LOG(ERROR) << "MemoryStatsFetcherImpl: No root resource"
                   << "present. Reconnecting...";
    InitializeRootResourceHandle();
    return false;
  }
  zx_status_t err =
      zx_object_get_info(root_resource_handle_, ZX_INFO_KMEM_STATS, mem_stats,
                         sizeof(*mem_stats), NULL, NULL);
  if (err != ZX_OK) {
    FXL_LOG(ERROR) << "MemoryStatsFetcherImpl: Fetching "
                   << "ZX_INFO_KMEM_STATS through syscall returns "
                   << zx_status_get_string(err);
    return false;
  }
  return true;
}

// TODO(CF-691) When Component Stats (CS) supports memory metrics,
// switch to Component Stats / iquery, by creating a new class with the
// interface MemoryStatsFetcher.
void MemoryStatsFetcherImpl::InitializeRootResourceHandle() {
  static const char kSysInfo[] = "/dev/misc/sysinfo";
  int fd = open(kSysInfo, O_RDWR);
  if (fd < 0) {
    FXL_LOG(ERROR)
        << "Cobalt SystemMetricsDaemon: Error getting root_resource_handle_. "
        << "Cannot open sysinfo: " << strerror(errno);
    return;
  }
  zx::channel channel;
  zx_status_t status =
      fdio_get_service_handle(fd, channel.reset_and_get_address());
  if (status != ZX_OK) {
    FXL_LOG(ERROR)
        << "Cobalt SystemMetricsDaemon: Error getting root_resource_handle_. "
        << "Cannot obtain sysinfo channel: " << zx_status_get_string(status);
    return;
  }
  zx_status_t fidl_status = fuchsia_sysinfo_DeviceGetRootResource(
      channel.get(), &status, &root_resource_handle_);
  if (fidl_status != ZX_OK) {
    FXL_LOG(ERROR)
        << "Cobalt SystemMetricsDaemon: Error getting root_resource_handle_. "
        << zx_status_get_string(fidl_status);
    return;
  } else if (status != ZX_OK) {
    FXL_LOG(ERROR)
        << "Cobalt SystemMetricsDaemon: Error getting root_resource_handle_. "
        << zx_status_get_string(status);
    return;
  } else if (root_resource_handle_ == ZX_HANDLE_INVALID) {
    FXL_LOG(ERROR)
        << "Cobalt SystemMetricsDaemon: Failed to get root_resource_handle_.";
    return;
  }
}

}  // namespace cobalt