// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BOARD_LIB_ACPI_DEVICE_FOR_HOST_H_
#define SRC_DEVICES_BOARD_LIB_ACPI_DEVICE_FOR_HOST_H_
#include <lib/ddk/driver.h>
#ifdef __Fuchsia__
#error "This file should not be built on Fuchsia."
#endif

#include "src/devices/board/lib/acpi/device-args.h"

namespace acpi {

class Device {
 public:
  explicit Device(DeviceArgs&& args) : Device(args) {}
  explicit Device(DeviceArgs& args) {}

  zx::status<> AddDevice(const char* name, cpp20::span<zx_device_prop_t> props,
                         cpp20::span<zx_device_str_prop_t> str_props, uint32_t flags);

  zx_status_t DdkAddComposite(const char* name, composite_device_desc_t* desc) {
    // Delete ourselves, because device-builder will immediately release the unique_ptr.
    delete this;
    return ZX_OK;
  }
  zx_device_t* zxdev();
};

}  // namespace acpi

struct zx_device : public acpi::Device {};

#endif
