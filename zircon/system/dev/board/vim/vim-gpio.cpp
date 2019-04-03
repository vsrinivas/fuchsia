// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/metadata/gpio.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>

#include <soc/aml-s905x/s905x-gpio.h>
#include <soc/aml-s912/s912-gpio.h>
#include <soc/aml-s912/s912-hw.h>

#include <limits.h>

#include "vim.h"

namespace vim {

// S905X and S912 have same MMIO addresses
static const pbus_mmio_t gpio_mmios[] = {
    {
        .base = S912_GPIO_BASE,
        .length = S912_GPIO_LENGTH,
    },
    {
        .base = S912_GPIO_AO_BASE,
        .length = S912_GPIO_AO_LENGTH,
    },
    {
        .base = S912_GPIO_INTERRUPT_BASE,
        .length = S912_GPIO_INTERRUPT_LENGTH,
    },
};

// S905X and S912 have same GPIO IRQ numbers
static const pbus_irq_t gpio_irqs[] = {
    {
        .irq = S912_GPIO_IRQ_0,
        .mode = 0,
    },
    {
        .irq = S912_GPIO_IRQ_1,
        .mode = 0,
    },
    {
        .irq = S912_GPIO_IRQ_2,
        .mode = 0,
    },
    {
        .irq = S912_GPIO_IRQ_3,
        .mode = 0,
    },
    {
        .irq = S912_GPIO_IRQ_4,
        .mode = 0,
    },
    {
        .irq = S912_GPIO_IRQ_5,
        .mode = 0,
    },
    {
        .irq = S912_GPIO_IRQ_6,
        .mode = 0,
    },
    {
        .irq = S912_GPIO_IRQ_7,
        .mode = 0,
    },
    {
        .irq = S912_AO_GPIO_IRQ_0,
        .mode = 0,
    },
    {
        .irq = S912_AO_GPIO_IRQ_1,
        .mode = 0,
    },
};

// GPIOs to expose from generic GPIO driver.
static const gpio_pin_t gpio_pins[] = {
    // For wifi.
    { S912_WIFI_SDIO_WAKE_HOST },
    { S912_GPIODV(13) },
};

static const pbus_metadata_t gpio_metadata[] = {
    {
        .type = DEVICE_METADATA_GPIO_PINS,
        .data_buffer = &gpio_pins,
        .data_size = sizeof(gpio_pins),
    }
};

zx_status_t Vim::GpioInit() {

    pbus_dev_t gpio_dev = {};
    gpio_dev.name = "gpio";
    gpio_dev.vid = PDEV_VID_AMLOGIC;
    gpio_dev.pid = PDEV_PID_AMLOGIC_S912;
    gpio_dev.did = PDEV_DID_AMLOGIC_GPIO;
    gpio_dev.mmio_list = gpio_mmios;
    gpio_dev.mmio_count = countof(gpio_mmios);
    gpio_dev.irq_list = gpio_irqs;
    gpio_dev.irq_count = countof(gpio_irqs);
    gpio_dev.metadata_list = gpio_metadata;
    gpio_dev.metadata_count = countof(gpio_metadata);

    zx_status_t status = pbus_.ProtocolDeviceAdd(ZX_PROTOCOL_GPIO_IMPL, &gpio_dev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "GpioInit: pbus_protocol_device_add failed: %d\n", status);
        return status;
    }

    gpio_impl_ = ddk::GpioImplProtocolClient(parent());
    if (!gpio_impl_.is_valid()) {
        zxlogf(ERROR, "%s: device_get_protocol failed\n", __func__);
        return ZX_ERR_INTERNAL;
    }

#if USE_GPIO_TEST
    const pbus_gpio_t gpio_test_gpios[] = {
        {
            // SYS_LED
            .gpio = S912_GPIOAO(9),
        },
        {
            // GPIO PIN
            .gpio = S912_GPIOAO(2),
        },
    };

    pbus_dev_t gpio_test_dev = {};
    gpio_test_dev.name = "vim-gpio-test";
    gpio_test_dev.vid = PDEV_VID_GENERIC;
    gpio_test_dev.pid = PDEV_PID_GENERIC;
    gpio_test_dev.did = PDEV_DID_GPIO_TEST;
    gpio_test_dev.gpio_list = gpio_test_gpios;
    gpio_test_dev.gpio_count = countof(gpio_test_gpios);

    status = pbus_.DeviceAdd(&gpio_test_dev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "GpioInit could not add gpio_test_dev: %d\n", status);
        return status;
    }
#else
    const pbus_gpio_t light_gpios[] = {
        {
            // SYS_LED
            .gpio = S912_GPIOAO(9),
        },
    };

    using light_name = char[ZX_MAX_NAME_LEN];
    static const light_name light_names[] = {
        "SYS_LED"
    };

    static const pbus_metadata_t light_metadata[] = {
        {
            .type = DEVICE_METADATA_NAME,
            .data_buffer = &light_names,
            .data_size = sizeof(light_names),
        },
    };

    pbus_dev_t light_dev = {};
    light_dev.name = "gpio-light";
    light_dev.vid = PDEV_VID_GENERIC;
    light_dev.pid = PDEV_PID_GENERIC;
    light_dev.did = PDEV_DID_GPIO_LIGHT;
    light_dev.gpio_list = light_gpios;
    light_dev.gpio_count = countof(light_gpios);
    light_dev.metadata_list = light_metadata;
    light_dev.metadata_count = countof(light_metadata);

    status = pbus_.DeviceAdd(&light_dev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "GpioInit could not add gpio_light_dev: %d\n", status);
        return status;
    }
#endif

    return ZX_OK;
}
} //namespace vim
