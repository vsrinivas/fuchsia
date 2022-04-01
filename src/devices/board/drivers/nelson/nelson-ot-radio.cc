// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>
#include <lib/ot-radio/ot-radio.h>

#include "nelson.h"
#include "src/devices/board/drivers/nelson/nelson_ot_radio_bind.h"

namespace {

constexpr uint32_t device_id = kOtDeviceNrf52811;
const pbus_metadata_t nrf52811_radio_metadata[] = {
    {
        .type = DEVICE_METADATA_PRIVATE,
        .data_buffer = reinterpret_cast<const uint8_t*>(&device_id),
        .data_size = sizeof(device_id),
    },
};

}  // namespace

namespace nelson {

zx_status_t Nelson::OtRadioInit() {
  pbus_dev_t dev = {};
  dev.name = "nrf52811-radio";
  dev.vid = PDEV_VID_GENERIC;
  dev.pid = PDEV_PID_NELSON;
  dev.did = PDEV_DID_OT_RADIO;
  dev.metadata_list = nrf52811_radio_metadata;
  dev.metadata_count = std::size(nrf52811_radio_metadata);

  zx_status_t status =
      pbus_.AddComposite(&dev, reinterpret_cast<uint64_t>(nrf52811_radio_fragments),
                         std::size(nrf52811_radio_fragments), "pdev");
  if (status != ZX_OK) {
    zxlogf(ERROR, "AddComposite failed: %s", zx_status_get_string(status));
  }
  return status;
}

}  // namespace nelson
