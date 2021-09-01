// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_ALLOCATION_RESULT_H_
#define SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_ALLOCATION_RESULT_H_

#include <fidl/fuchsia.sysmem2/cpp/wire.h>
#include <zircon/types.h>

namespace sysmem_driver {

struct AllocationResult {
  const fuchsia_sysmem2::wire::BufferCollectionInfo* buffer_collection_info = nullptr;
  const zx_status_t status = ZX_OK;
};

}  // namespace sysmem_driver

#endif  // SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_ALLOCATION_RESULT_H_
