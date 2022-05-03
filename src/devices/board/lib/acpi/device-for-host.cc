// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/board/lib/acpi/device-for-host.h"

namespace acpi {

zx_device_t* Device::zxdev() { return static_cast<zx_device_t*>(this); }

zx::status<> Device::AddDevice(const char* name, cpp20::span<zx_device_prop_t> props,
                               cpp20::span<zx_device_str_prop_t> str_props, uint32_t flags) {
  printf("Added device '%s'\n", name);
  return zx::ok();
}

}  // namespace acpi
