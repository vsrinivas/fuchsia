// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/misc/drivers/compat/sysmem.h"

#include "src/devices/misc/drivers/compat/driver.h"

namespace compat {

zx_status_t Sysmem::SysmemConnect(zx::channel allocator_request) {
  return driver_->driver_namespace()
      .Connect(fidl::DiscoverableProtocolDefaultPath<fuchsia_sysmem::Allocator>,
               std::move(allocator_request))
      .status_value();
}

zx_status_t Sysmem::SysmemRegisterHeap(uint64_t heap, zx::channel heap_connection) {
  FDF_LOGL(WARNING, driver_->logger(), "%s - not implemented in the compat shim.", __func__);
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Sysmem::SysmemRegisterSecureMem(zx::channel secure_mem_connection) {
  FDF_LOGL(WARNING, driver_->logger(), "%s - not implemented in the compat shim.", __func__);
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Sysmem::SysmemUnregisterSecureMem() {
  FDF_LOGL(WARNING, driver_->logger(), "%s - not implemented in the compat shim.", __func__);
  return ZX_ERR_NOT_SUPPORTED;
}

}  // namespace compat
