// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/platform/bus/c/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/platform-defs.h>

#include <lib/ddk/metadata.h>
#include <ddk/metadata/clock.h>
#include <soc/msm8x53/msm8x53-clock.h>
#include <soc/msm8x53/msm8x53-gpio.h>
#include <soc/msm8x53/msm8x53-hw.h>

#include "msm8x53.h"

namespace board_msm8x53 {

namespace {

constexpr pbus_mmio_t clock_mmios[] = {
    {
        .base = msm8x53::kCcBase,
        .length = msm8x53::kCcSize,
    },
};

constexpr clock_id_t clock_ids[] = {
    // For PIL.
    {msm8x53::kCryptoAhbClk},
    {msm8x53::kCryptoAxiClk},
    {msm8x53::kCryptoClk},
};

const pbus_metadata_t clock_metadata[] = {
    {
        .type = DEVICE_METADATA_CLOCK_IDS,
        .data_buffer = reinterpret_cast<const uint8_t*>(&clock_ids),
        .data_size = sizeof(clock_ids),
    },
};

constexpr pbus_dev_t clock_dev = []() {
  pbus_dev_t result{};

  result.name = "gcc-clock";
  result.vid = PDEV_VID_QUALCOMM;
  result.pid = PDEV_PID_QUALCOMM_MSM8X53;
  result.did = PDEV_DID_QUALCOMM_CLOCK;
  result.mmio_list = clock_mmios;
  result.mmio_count = countof(clock_mmios);
  result.metadata_list = clock_metadata;
  result.metadata_count = countof(clock_metadata);

  return result;
}();

}  // namespace

zx_status_t Msm8x53::ClockInit() {
  zx_status_t status = pbus_.DeviceAdd(&clock_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DeviceAdd failed %d", __func__, status);
    return status;
  }

  return ZX_OK;
}

}  // namespace board_msm8x53
