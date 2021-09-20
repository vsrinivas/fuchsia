// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/platform/bus/c/banjo.h>
#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>
#include <lib/ot-radio/ot-radio.h>
#include <limits.h>
#include <unistd.h>

#include <fbl/algorithm.h>
#include <soc/aml-t931/t931-gpio.h>
#include <soc/aml-t931/t931-hw.h>

#include "sherlock-gpios.h"
#include "sherlock.h"
#include "src/devices/board/drivers/sherlock/sherlock-ot-radio-bind.h"

namespace sherlock {

static const uint32_t device_id = kOtDeviceNrf52840;
static const pbus_metadata_t nrf52840_radio_metadata[] = {
    {.type = DEVICE_METADATA_PRIVATE,
     .data_buffer = reinterpret_cast<const uint8_t*>(&device_id),
     .data_size = sizeof(device_id)},
};

zx_status_t Sherlock::OtRadioInit() {
  pbus_dev_t dev = {};
  dev.name = "nrf52840-radio";
  dev.vid = PDEV_VID_GENERIC;
  dev.pid = PDEV_PID_SHERLOCK;
  dev.did = PDEV_DID_OT_RADIO;
  dev.metadata_list = nrf52840_radio_metadata;
  dev.metadata_count = std::size(nrf52840_radio_metadata);

  zx_status_t status =
      pbus_.AddComposite(&dev, reinterpret_cast<uint64_t>(nrf52840_radio_fragments),
                         std::size(nrf52840_radio_fragments), "pdev");
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s(nrf52840): AddComposite failed: %d", __func__, status);
  } else {
    zxlogf(INFO, "%s(nrf52840): AddComposite", __func__);
  }
  return status;
}

}  // namespace sherlock
