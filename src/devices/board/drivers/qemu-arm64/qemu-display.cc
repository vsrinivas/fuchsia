// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>

#include "qemu-bus.h"
#include "qemu-virt.h"
#include "zircon/driver/binding.h"
namespace board_qemu_arm64 {
static const zx_bind_inst_t root_match[] = {
    BI_MATCH(),
};
static const zx_bind_inst_t sysmem_match[] = {
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_SYSMEM),
};
static const device_fragment_part_t sysmem_fragment[] = {
    {countof(root_match), root_match},
    {countof(sysmem_match), sysmem_match},
};
static const device_fragment_t fragments[] = {
    {countof(sysmem_fragment), sysmem_fragment},
};
zx_status_t QemuArm64::DisplayInit() {
  pbus_dev_t display_dev = {};
  display_dev.name = "display";
  display_dev.vid = PDEV_VID_GENERIC;
  display_dev.pid = PDEV_PID_GENERIC;
  display_dev.did = PDEV_DID_FAKE_DISPLAY;
  auto status = pbus_.CompositeDeviceAdd(&display_dev, fragments, countof(fragments), UINT32_MAX);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DeviceAdd failed %d", __func__, status);
    return status;
  }
  return ZX_OK;
}

}  // namespace board_qemu_arm64
