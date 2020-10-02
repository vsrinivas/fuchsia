// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ot-radio/ot-radio.h>
#include <limits.h>
#include <unistd.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>
#include <fbl/algorithm.h>
#include <soc/aml-t931/t931-gpio.h>
#include <soc/aml-t931/t931-hw.h>

#include "sherlock-gpios.h"
#include "sherlock.h"

namespace sherlock {

static const uint32_t device_id = kOtDeviceNrf52840;
static const pbus_metadata_t nrf52840_radio_metadata[] = {
    {.type = DEVICE_METADATA_PRIVATE, .data_buffer = &device_id, .data_size = sizeof(device_id)},
};

// Composite binding rules for openthread radio driver.
static const zx_bind_inst_t root_match[] = {
    BI_MATCH(),
};
static constexpr zx_bind_inst_t ot_dev_match[] = {
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_NORDIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_NORDIC_NRF52840),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_NORDIC_THREAD),
};
static constexpr zx_bind_inst_t gpio_int_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
    BI_MATCH_IF(EQ, BIND_GPIO_PIN, GPIO_OT_RADIO_INTERRUPT),
};
static constexpr zx_bind_inst_t gpio_reset_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
    BI_MATCH_IF(EQ, BIND_GPIO_PIN, GPIO_OT_RADIO_RESET),
};
static constexpr zx_bind_inst_t gpio_bootloader_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
    BI_MATCH_IF(EQ, BIND_GPIO_PIN, GPIO_OT_RADIO_BOOTLOADER),
};
static constexpr device_fragment_part_t ot_dev_fragment[] = {
    {std::size(root_match), root_match},
    {std::size(ot_dev_match), ot_dev_match},
};
static constexpr device_fragment_part_t gpio_int_fragment[] = {
    {std::size(root_match), root_match},
    {std::size(gpio_int_match), gpio_int_match},
};
static constexpr device_fragment_part_t gpio_reset_fragment[] = {
    {std::size(root_match), root_match},
    {std::size(gpio_reset_match), gpio_reset_match},
};
static constexpr device_fragment_part_t gpio_bootloader_fragment[] = {
    {std::size(root_match), root_match},
    {std::size(gpio_bootloader_match), gpio_bootloader_match},
};
static constexpr device_fragment_new_t ot_fragments[] = {
    {"ot-radio", std::size(ot_dev_fragment), ot_dev_fragment},
    {"gpio-int", std::size(gpio_int_fragment), gpio_int_fragment},
    {"gpio-reset", std::size(gpio_reset_fragment), gpio_reset_fragment},
    {"gpio-bootloader", std::size(gpio_bootloader_fragment), gpio_bootloader_fragment},
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
      pbus_.CompositeDeviceAddNew(&dev, ot_fragments, std::size(ot_fragments), UINT32_MAX);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s(nrf52840): DeviceAdd failed: %d", __func__, status);
  } else {
    zxlogf(INFO, "%s(nrf52840): DeviceAdded", __func__);
  }
  return status;
}

}  // namespace sherlock
