// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/platform-defs.h>

#include <array>

#include "src/devices/board/drivers/x86/goldfish_control_2_bind.h"
#include "src/devices/board/drivers/x86/x86.h"

#define PCI_VID_GOLDFISH_ADDRESS_SPACE 0x607D
#define PCI_DID_GOLDFISH_ADDRESS_SPACE 0xF153

namespace x86 {

constexpr zx_device_prop_t props[] = {
    {BIND_PLATFORM_DEV_VID, 0, PDEV_VID_GOOGLE},
    {BIND_PLATFORM_DEV_PID, 0, PDEV_PID_GOLDFISH},
    {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_GOLDFISH_CONTROL},
};

static const composite_device_desc_t comp_desc = {
    .props = props,
    .props_count = std::size(props),
    .fragments = goldfish_control_2_fragments,
    .fragments_count = std::size(goldfish_control_2_fragments),
    .primary_fragment = "goldfish-pipe",
    .spawn_colocated = false,
};

zx_status_t X86::GoldfishControlInit() {
  zx_status_t status = DdkAddComposite("goldfish-control-2", &comp_desc);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s(goldfish-control-2): DdkAddComposite failed: %d", __func__, status);
    return status;
  }
  return status;
}

}  // namespace x86
