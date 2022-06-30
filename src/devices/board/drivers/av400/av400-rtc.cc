// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/platform/bus/c/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/platform-defs.h>

#include <soc/aml-a5/a5-hw.h>

#include "av400.h"

namespace av400 {

zx_status_t Av400::RtcInit() {
  static constexpr pbus_mmio_t rtc_mmios[] = {
      {
          .base = A5_RTC_BASE,
          .length = A5_RTC_LENGTH,
      },
  };

  static constexpr pbus_irq_t rtc_irqs[] = {
      {
          .irq = A5_RTC_IRQ,
          .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
      },
  };

  pbus_dev_t amlrtc_dev = {};
  amlrtc_dev.name = "amlrtc";
  amlrtc_dev.vid = PDEV_VID_AMLOGIC;
  amlrtc_dev.pid = PDEV_PID_AMLOGIC_A5;
  amlrtc_dev.did = PDEV_DID_AMLOGIC_RTC;
  amlrtc_dev.mmio_list = rtc_mmios;
  amlrtc_dev.mmio_count = std::size(rtc_mmios);
  amlrtc_dev.irq_list = rtc_irqs;
  amlrtc_dev.irq_count = std::size(rtc_irqs);

  auto status = pbus_.DeviceAdd(&amlrtc_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DeviceAdd failed %d", __func__, status);
    return status;
  }

  return ZX_OK;
}

}  // namespace av400
