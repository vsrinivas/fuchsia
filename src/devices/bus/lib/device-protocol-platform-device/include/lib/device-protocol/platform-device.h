// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BUS_LIB_DEVICE_PROTOCOL_PLATFORM_DEVICE_INCLUDE_LIB_DEVICE_PROTOCOL_PLATFORM_DEVICE_H_
#define SRC_DEVICES_BUS_LIB_DEVICE_PROTOCOL_PLATFORM_DEVICE_INCLUDE_LIB_DEVICE_PROTOCOL_PLATFORM_DEVICE_H_

#include <zircon/compiler.h>
#include <zircon/types.h>

#include <ddk/io-buffer.h>
#include <ddk/mmio-buffer.h>
#include <ddk/protocol/platform/device.h>

__BEGIN_CDECLS

// MMIO mapping helper.
static inline zx_status_t pdev_map_mmio_buffer(const pdev_protocol_t* pdev, uint32_t index,
                                               uint32_t cache_policy, mmio_buffer_t* buffer) {
  pdev_mmio_t mmio;

  zx_status_t status = pdev_get_mmio(pdev, index, &mmio);
  if (status != ZX_OK) {
    return status;
  }
  return mmio_buffer_init(buffer, mmio.offset, mmio.size, mmio.vmo, cache_policy);
}

__END_CDECLS

#endif  // SRC_DEVICES_BUS_LIB_DEVICE_PROTOCOL_PLATFORM_DEVICE_INCLUDE_LIB_DEVICE_PROTOCOL_PLATFORM_DEVICE_H_
