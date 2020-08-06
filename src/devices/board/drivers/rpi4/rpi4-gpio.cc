// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// for the time being it's only a template copy from vim3

#include "rpi4.h"

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/metadata.h>
#include <ddk/metadata/gpio.h>
#include <ddk/platform-defs.h>

#include <soc/bcm2711/bcm2711-gpio.h>
#include <soc/bcm2711/bcm2711-hw.h>
#include "rpi4-gpios.h"

#define GPIO_TEST

namespace rpi4 {

static const pbus_mmio_t gpio_mmios[] = {
    {
        // manage in one contiguous space
        .base = BCM2711_GPIO_BASE,
        .length = BCM2711_GPIO_LENGTH,
    },
};

static const pbus_irq_t gpio_irqs[] = {
    {
        .irq = BCM2711_GPIO_IRQ_0,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    },
    {
        .irq = BCM2711_GPIO_IRQ_1,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    },
};

// GPIOs to expose from generic GPIO driver.
static const gpio_pin_t gpio_pins[] = {
      {BCM2711_GPIO_PIN(21)},
      {BCM2711_GPIO_PIN(20)},
      {BCM2711_GPIO_PIN(16)},
};

static const pbus_metadata_t gpio_metadata[] = {
    {
        .type = DEVICE_METADATA_GPIO_PINS,
        .data_buffer = &gpio_pins,
        .data_size = sizeof(gpio_pins),
    },
};

static pbus_dev_t gpio_dev = []() {
  pbus_dev_t dev = {};
  dev.name = "gpio";
  dev.vid = PDEV_VID_BROADCOM;
  dev.pid = PDEV_PID_BCM2711;
  dev.did = PDEV_DID_BCM_GPIO;
  dev.mmio_list = gpio_mmios;
  dev.mmio_count = countof(gpio_mmios);
  dev.irq_list = gpio_irqs;
  dev.irq_count = countof(gpio_irqs);
  dev.metadata_list = gpio_metadata;
  dev.metadata_count = countof(gpio_metadata);
  return dev;
}();

zx_status_t Rpi4::GpioInit() {

  zx_status_t status = pbus_.ProtocolDeviceAdd(ZX_PROTOCOL_GPIO_IMPL, &gpio_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: ProtocolDeviceAdd failed %d", __func__, status);
    return status;
  }

  gpio_impl_ = ddk::GpioImplProtocolClient(parent());
  if (!gpio_impl_.is_valid()) {
    zxlogf(ERROR, "%s: device_get_protocol failed %d", __func__, status);
    return ZX_ERR_INTERNAL;
  }

#ifdef GPIO_TEST

  static const zx_bind_inst_t root_match[] = {
      BI_MATCH(),
  };
  static const zx_bind_inst_t gpio_button_match[] = {
      BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
      BI_MATCH_IF(EQ, BIND_GPIO_PIN, BCM2711_GPIO_PIN(21)),
  };
  static const zx_bind_inst_t gpio_led_match[] = {
      BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
      BI_MATCH_IF(EQ, BIND_GPIO_PIN, BCM2711_GPIO_PIN(20)),
  };
  static const device_fragment_part_t gpio_button_fragment[] = {
      {countof(root_match), root_match},
      {countof(gpio_button_match), gpio_button_match},
  };
  static const device_fragment_part_t gpio_led_fragment[] = {
      {countof(root_match), root_match},
      {countof(gpio_led_match), gpio_led_match},
  };
  static const device_fragment_t fragments[] = {
      // according to gpio-test.h's enum { GPIO_LED, GPIO_BUTTON,}
      {countof(gpio_led_fragment), gpio_led_fragment},
      {countof(gpio_button_fragment), gpio_button_fragment},
  };

  constexpr zx_device_prop_t props[] = {
      {BIND_PLATFORM_DEV_VID, 0, PDEV_VID_GENERIC},
      {BIND_PLATFORM_DEV_PID, 0, PDEV_PID_GENERIC},
      {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_GPIO_TEST},
  };

  const composite_device_desc_t comp_desc = {
      .props = props,
      .props_count = countof(props),
      .fragments = fragments,
      .fragments_count = countof(fragments),
      .coresident_device_index = 0,
      .metadata_list = nullptr,
      .metadata_count = 0,
  };

  status = DdkAddComposite("gpio-test", &comp_desc);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: CompositeDeviceAdd failed: %d", __func__, status);
    return status;
  }

#endif

  return ZX_OK;
}

}  // namespace rpi4
