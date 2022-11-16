// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>

#include "buckeye-gpios.h"
#include "buckeye.h"
#include "src/devices/board/drivers/buckeye/buckeye-uwb-bind.h"
#include "src/devices/bus/lib/platform-bus-composites/platform-bus-composite.h"

namespace buckeye {
namespace fpbus = fuchsia_hardware_platform_bus;

zx_status_t Buckeye::UWBInit() {
  const zx_device_prop_t props[] = {{BIND_PLATFORM_DEV_VID, 0, PDEV_VID_NXP},
                                    {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_SR1XX}};

  const composite_device_desc_t comp_desc = {
      .props = props,
      .props_count = std::size(props),
      .fragments = sr1xx_fragments,
      .fragments_count = std::size(sr1xx_fragments),
      .primary_fragment = "spi",
      .spawn_colocated = false,
      .metadata_list = nullptr,
      .metadata_count = 0,
  };

  zx_status_t status = DdkAddComposite("uwb", &comp_desc);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DdkAddComposite failed: %d", __func__, status);
    return status;
  }

  zxlogf(INFO, "Added UWBDevice");

  return ZX_OK;
}

}  // namespace buckeye
