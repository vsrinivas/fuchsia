// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/tests/node-group-test/drivers/node-group-driver.h"

#include <lib/ddk/device.h>
#include <lib/ddk/metadata.h>

#include "src/devices/tests/node-group-test/drivers/node-group-driver-bind.h"

namespace node_group_driver {

// static
zx_status_t NodeGroupDriver::Bind(void* ctx, zx_device_t* device) {
  auto dev = std::make_unique<NodeGroupDriver>(device);

  // Verify the metadata.
  char metadata[32] = "";
  size_t len = 0;
  auto status = dev->DdkGetMetadata(DEVICE_METADATA_PRIVATE, &metadata, std::size(metadata), &len);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to read metadata %d", status);
    return status;
  }

  constexpr char kMetadataStr[] = "node-group-metadata";
  if (strlen(kMetadataStr) + 1 != len) {
    zxlogf(ERROR, "Incorrect metadata size: %zu", strlen(kMetadataStr));
    return ZX_ERR_INTERNAL;
  }

  if (strcmp(kMetadataStr, metadata) != 0) {
    zxlogf(ERROR, "Incorrect metadata value: %s", metadata);
    return ZX_ERR_INTERNAL;
  }

  status = dev->DdkAdd("node_group");
  if (status != ZX_OK) {
    return status;
  }

  __UNUSED auto ptr = dev.release();
  return ZX_OK;
}

static zx_driver_ops_t kDriverOps = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = NodeGroupDriver::Bind;
  return ops;
}();

}  // namespace node_group_driver

ZIRCON_DRIVER(node_group_driver, node_group_driver::kDriverOps, "zircon", "0.1");
