// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/metadata/gpio.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>

#include <soc/aml-s905d2/s905d2-gpio.h>
#include <soc/aml-s905d2/s905d2-hw.h>

#include <limits.h>

#include "astro.h"
#include "astro-gpios.h"

// uncomment to disable LED blinky test
// #define GPIO_TEST 1

static const pbus_mmio_t gpio_mmios[] = {
    {
        .base = S905D2_GPIO_BASE,
        .length = S905D2_GPIO_LENGTH,
    },
    {
        .base = S905D2_GPIO_A0_BASE,
        .length = S905D2_GPIO_AO_LENGTH,
    },
    {
        .base = S905D2_GPIO_INTERRUPT_BASE,
        .length = S905D2_GPIO_INTERRUPT_LENGTH,
    },
};

static const pbus_irq_t gpio_irqs[] = {
    {
        .irq = S905D2_GPIO_IRQ_0,
    },
    {
        .irq = S905D2_GPIO_IRQ_1,
    },
    {
        .irq = S905D2_GPIO_IRQ_2,
    },
    {
        .irq = S905D2_GPIO_IRQ_3,
    },
    {
        .irq = S905D2_GPIO_IRQ_4,
    },
    {
        .irq = S905D2_GPIO_IRQ_5,
    },
    {
        .irq = S905D2_GPIO_IRQ_6,
    },
    {
        .irq = S905D2_GPIO_IRQ_7,
    },
    /*
    {
        .irq = S905D2_A0_GPIO_IRQ_0,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = S905D2_A0_GPIO_IRQ_1,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    */
};

// GPIOs to expose from generic GPIO driver.
static const gpio_pin_t gpio_pins[] = {
    // For wifi.
    { S905D2_WIFI_SDIO_WAKE_HOST },
    // For display.
    { GPIO_PANEL_DETECT },
    { GPIO_LCD_RESET },
    // For touch screen.
    { GPIO_TOUCH_INTERRUPT },
    { GPIO_TOUCH_RESET },
    // For light sensor.
    { GPIO_LIGHT_INTERRUPT },
};

static const pbus_metadata_t gpio_metadata[] = {
    {
        .type = DEVICE_METADATA_GPIO_PINS,
        .data_buffer = &gpio_pins,
        .data_size = sizeof(gpio_pins),
    }
};

static pbus_dev_t gpio_dev = {
    .name = "gpio",
    .vid = PDEV_VID_AMLOGIC,
    .pid = PDEV_PID_AMLOGIC_S905D2,
    .did = PDEV_DID_AMLOGIC_GPIO,
    .mmio_list = gpio_mmios,
    .mmio_count = countof(gpio_mmios),
    .irq_list = gpio_irqs,
    .irq_count = countof(gpio_irqs),
    .metadata_list = gpio_metadata,
    .metadata_count = countof(gpio_metadata),
};

zx_status_t aml_gpio_init(aml_bus_t* bus) {
    zx_status_t status = pbus_protocol_device_add(&bus->pbus, ZX_PROTOCOL_GPIO_IMPL, &gpio_dev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml_gpio_init: pbus_protocol_device_add failed: %d\n", status);
        return status;
    }

    status = device_get_protocol(bus->parent, ZX_PROTOCOL_GPIO_IMPL, &bus->gpio);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml_gpio_init: device_get_protocol failed: %d\n", status);
        return status;
    }

    // Enable mute LED so it will be controlled by mute switch.
    status = gpio_impl_config_out(&bus->gpio, S905D2_GPIOAO(11), 1);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml_gpio_init: gpio_impl_config_out failed: %d\n", status);
    }

#if GPIO_TEST
    const pbus_gpio_t gpio_test_gpios[] = {
        {
            // SYS_LED
            .gpio = S905D2_GPIOAO(11),
        },
        {
            // JTAG Adapter Pin
            .gpio = S905D2_GPIOAO(6),
        }
    };

    const pbus_dev_t gpio_test_dev = {
        .name = "aml-gpio-test",
        .vid = PDEV_VID_GENERIC,
        .pid = PDEV_PID_GENERIC,
        .did = PDEV_DID_GPIO_TEST,
        .gpio_list = gpio_test_gpios,
        .gpio_count = countof(gpio_test_gpios),
    };

    if ((status = pbus_device_add(&bus->pbus, &gpio_test_dev, 0)) != ZX_OK) {
        zxlogf(ERROR, "aml_gpio_init could not add gpio_test_dev: %d\n", status);
        return status;
    }
#endif

    return ZX_OK;
}
