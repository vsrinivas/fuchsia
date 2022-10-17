// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/kernel/cpp/fidl.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/zx/status.h>

#include <cstdint>
#include <memory>

namespace hwstress {

zx::result<fuchsia::kernel::MemoryStats> GetMemoryStats() {
  std::shared_ptr<sys::ServiceDirectory> svc = sys::ServiceDirectory::CreateFromNamespace();
  if (svc == nullptr) {
    return zx::error(ZX_ERR_UNAVAILABLE);
  }

  fuchsia::kernel::StatsSyncPtr stats_ptr;
  zx_status_t status = svc->Connect(stats_ptr.NewRequest());
  if (status != ZX_OK) {
    return zx::error(status);
  }

  fuchsia::kernel::MemoryStats stats;
  status = stats_ptr->GetMemoryStats(&stats);
  if (status != ZX_OK) {
    return zx::error(status);
  }
  return zx::ok(std::move(stats));
}

}  // namespace hwstress
