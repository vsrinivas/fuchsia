// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/platform/bus/c/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/platform-defs.h>

#include <soc/msm8x53/msm8x53-gpio.h>
#include <soc/msm8x53/msm8x53-hw.h>

#include "msm8x53.h"

namespace board_msm8x53 {

zx_status_t Msm8x53::GpioInit() {
  const pbus_mmio_t gpio_mmios[] = {
      {
          .base = msm8x53::kGpioBase,
          .length = msm8x53::kGpioSize,
      },
  };

  const pbus_irq_t gpio_irqs[] = {
      {
          .irq = msm8x53::kIrqCombined,
          .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
      },
  };

  pbus_dev_t gpio_dev = {};
  gpio_dev.name = "gpio";
  gpio_dev.vid = PDEV_VID_QUALCOMM;
  gpio_dev.pid = PDEV_PID_QUALCOMM_MSM8X53;
  gpio_dev.did = PDEV_DID_QUALCOMM_GPIO;
  gpio_dev.mmio_list = gpio_mmios;
  gpio_dev.mmio_count = countof(gpio_mmios);
  gpio_dev.irq_list = gpio_irqs;
  gpio_dev.irq_count = countof(gpio_irqs);

  zx_status_t status = pbus_.ProtocolDeviceAdd(ZX_PROTOCOL_GPIO_IMPL, &gpio_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: ProtocolDeviceAdd failed %d", __func__, status);
    return status;
  }

// This test binds to system/dev/gpio/gpio-test to check that GPIOs work at all.
// gpio-test enables interrupts and write/read on the test GPIOs configured below.
//#define GPIO_TEST
#ifdef GPIO_TEST
  const pbus_gpio_t gpio_test_gpios[] = {
      {
          .gpio = 0,  // TODO(andresoportus) find a pin to test gpio_write().
      },
      {
          .gpio = 85,  // Key + to test gpio_get_interrupt().
      },
  };

  pbus_dev_t gpio_test_dev = {};
  gpio_test_dev.name = "msm8x53-gpio-test";
  gpio_test_dev.vid = PDEV_VID_GENERIC;
  gpio_test_dev.pid = PDEV_PID_GENERIC;
  gpio_test_dev.did = PDEV_DID_GPIO_TEST;
  gpio_test_dev.gpio_list = gpio_test_gpios;
  gpio_test_dev.gpio_count = countof(gpio_test_gpios);
  if ((status = pbus_.DeviceAdd(&gpio_test_dev)) != ZX_OK) {
    zxlogf(ERROR, "%s: Could not add gpio_test_dev %d", __func__, status);
    return status;
  }
#endif

  return ZX_OK;
}

}  // namespace board_msm8x53
