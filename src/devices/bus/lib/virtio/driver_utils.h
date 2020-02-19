// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BUS_LIB_VIRTIO_DRIVER_UTILS_H_
#define SRC_DEVICES_BUS_LIB_VIRTIO_DRIVER_UTILS_H_

#include <lib/fit/result.h>
#include <stdlib.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <type_traits>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>

#include "backends/pci.h"
#include "device.h"

// Get the bti and virtio backend for a given pci virtio device.
fit::result<std::pair<zx::bti, std::unique_ptr<virtio::Backend>>, zx_status_t> GetBtiAndBackend(
    zx_device_t* bus_device);

// Creates a virtio device, calls DdkAdd, and releases it to the dev_mgr.
template <class VirtioDevice, class = typename std::enable_if<
                                  std::is_base_of<virtio::Device, VirtioDevice>::value>::type>
zx_status_t CreateAndBind(void* /*ctx*/, zx_device_t* device) {
  auto bti_and_backend = GetBtiAndBackend(device);
  if (!bti_and_backend.is_ok()) {
    return bti_and_backend.error();
  }
  auto dev = std::make_unique<VirtioDevice>(device, std::move(bti_and_backend.value().first),
                                            std::move(bti_and_backend.value().second));
  zx_status_t status = dev->Init();
  if (status == ZX_OK) {
    // devmgr is now in charge of the memory for dev
    __UNUSED auto ptr = dev.release();
  }
  return status;
}

#endif  // SRC_DEVICES_BUS_LIB_VIRTIO_DRIVER_UTILS_H_
