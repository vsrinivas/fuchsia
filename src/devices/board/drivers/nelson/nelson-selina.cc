// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/platform-defs.h>
#include <zircon/status.h>

#include "nelson-gpios.h"
#include "nelson.h"
#include "src/devices/board/drivers/nelson/nelson_selina_bind.h"

namespace nelson {

static constexpr zx_device_prop_t props[] = {
    {BIND_PLATFORM_DEV_VID, 0, PDEV_VID_GENERIC},
    {BIND_PLATFORM_DEV_PID, 0, PDEV_PID_NELSON},
    {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_RADAR_SENSOR},
};

static composite_device_desc_t composite_dev = []() {
  composite_device_desc_t desc = {};
  desc.props = props;
  desc.props_count = std::size(props);
  desc.fragments = selina_fragments;
  desc.fragments_count = std::size(selina_fragments);
  desc.primary_fragment = "spi";
  desc.spawn_colocated = true;
  return desc;
}();

zx_status_t Nelson::SelinaInit() { return DdkAddComposite("selina", &composite_dev); }

}  // namespace nelson
