// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <soc/hi3660/hi3660-hw.h>

#include <stdio.h>

#include "hikey960.h"
#include "hikey960-hw.h"

// #define GPIO_TEST 1
// #define I2C_TEST 1
//#define DSI_ENABLE 1

#ifdef DSI_ENABLE
static const pbus_mmio_t dsi_mmios[] = {
    {
        .base = MMIO_DSI_BASE,
        .length = MMIO_DSI_LENGTH,
    },
};

static const pbus_i2c_channel_t dsi.i2c_channel_list[] = {
    {
        // HDMI_MAIN
        .bus_id = DW_I2C_1,
        .address = 0x39,
    },
    {
        // HDMI_CEC
        .bus_id = DW_I2C_1,
        .address = 0x38,
    },
    {
        // HDMI_EDID
        .bus_id = DW_I2C_1,
        .address = 0x3b,
    },
};

static const pbus_gpio_t dsi_gpios[] = {
    {
        .gpio = GPIO_HDMI_MUX,
    },
    {
        .gpio = GPIO_HDMI_PD,
    },
    {
        .gpio = GPIO_HDMI_INT,
    },
};

static const pbus_bti_t dsi_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_DSI,
    },
};

static const pbus_dev_t dsi_dev = {
    .name = "dsi",
    .vid = PDEV_VID_GENERIC,
    .pid = PDEV_PID_GENERIC,
    .did = PDEV_DID_DSI,
    .mmio_list = dsi_mmios,
    .mmio_count = countof(dsi_mmios),
    .i2c_channel_list = dsi.i2c_channel_list,
    .i2c_channel_count = countof(dsi.i2c_channel_list),
    .gpio_list = dsi_gpios,
    .gpio_count = countof(dsi_gpios),
    .bti_list = dsi_btis,
    .bti_count = countof(dsi_btis),
};
#endif

static const pbus_mmio_t mali_mmios[] = {
    {
        .base = MMIO_G3D_BASE,
        .length = MMIO_G3D_LENGTH,
    },
};

static const pbus_irq_t mali_irqs[] = {
    {
        .irq = IRQ_G3D_JOB,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
    {
        .irq = IRQ_G3D_MMU,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
    {
        .irq = IRQ_G3D_GPU,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
};

static const pbus_bti_t mali_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_MALI,
    },
};

static const pbus_dev_t mali_dev = {
    .name = "mali",
    .vid = PDEV_VID_GENERIC,
    .pid = PDEV_PID_GENERIC,
    .did = PDEV_DID_ARM_MALI,
    .mmio_list = mali_mmios,
    .mmio_count = countof(mali_mmios),
    .irq_list = mali_irqs,
    .irq_count = countof(mali_irqs),
    .bti_list = mali_btis,
    .bti_count = countof(mali_btis),
};

static const pbus_mmio_t clk_mmios[] = {
    {
        .base = MMIO_PERI_CRG_BASE,
        .length = MMIO_PERI_CRG_LENGTH,
    },
    {
        .base = MMIO_SCTRL_BASE,
        .length = MMIO_SCTRL_LENGTH,
    },
};

static const pbus_dev_t hi3660_clk_dev = {
    .name = "hi3660-clk",
    .vid = PDEV_VID_96BOARDS,
    .did = PDEV_DID_HI3660_CLK,
    .mmio_list = clk_mmios,
    .mmio_count = countof(clk_mmios),
};

#if GPIO_TEST
static const pbus_gpio_t gpio_test_gpios[] = {
    {
        .gpio = GPIO_USER_LED3,
    },
    {
        .gpio = GPIO_USER_LED1,
    },
    {
        .gpio = GPIO_USER_LED2,
    },
    {
        .gpio = GPIO_USER_LED4,
    },
};

static const pbus_dev_t gpio_test_dev = {
    .name = "hikey960-gpio-test",
    .vid = PDEV_VID_GENERIC,
    .pid = PDEV_PID_GENERIC,
    .did = PDEV_DID_GPIO_TEST,
    .gpio_list = gpio_test_gpios,
    .gpio_count = countof(gpio_test_gpios),
};
#endif

#if I2C_TEST
static const pbus_i2c_channel_t i2c_test_channels[] = {
    {
        // USB HUB
        .bus_id = DW_I2C_1,
        .address = 0x4e,
    },
};

static const pbus_dev_t i2c_test_dev = {
    .name = "hikey960-i2c-test",
    .vid = PDEV_VID_96BOARDS,
    .pid = PDEV_PID_HIKEY960,
    .did = PDEV_DID_HIKEY960_I2C_TEST,
    .i2c_channel_list = i2c_test_channels,
    .i2c_channel_count = countof(i2c_test_channels),
};
#endif

static const pbus_dev_t dummy_display_dev = {
    .name = "dummy-display",
    .vid = PDEV_VID_GENERIC,
    .did = PDEV_DID_DUMMY_DISPLAY,
};

zx_status_t hikey960_add_devices(hikey960_t* hikey) {
    zx_status_t status;

    if ((status = pbus_protocol_device_add(&hikey->pbus, ZX_PROTOCOL_CLK, &hi3660_clk_dev))
            != ZX_OK) {
        zxlogf(ERROR, "hikey960_add_devices could not add clk_dev: %d\n", status);
    }
    if ((status = hikey960_usb_init(hikey)) != ZX_OK) {
        zxlogf(ERROR, "hikey960_usb_init failed: %d\n", status);
    }
    if ((status = pbus_device_add(&hikey->pbus, &mali_dev)) != ZX_OK) {
        zxlogf(ERROR, "hikey960_add_devices could not add mali_dev: %d\n", status);
        return status;
    }
#ifdef DSI_ENABLE
    if ((status = pbus_device_add(&hikey->pbus, &dsi_dev)) != ZX_OK) {
        zxlogf(ERROR, "hikey960_add_devices could not add dsi_dev: %d\n", status);
    }
#endif

#if GPIO_TEST
    if ((status = pbus_device_add(&hikey->pbus, &gpio_test_dev)) != ZX_OK) {
        zxlogf(ERROR, "hikey960_add_devices could not add gpio_test_dev: %d\n", status);
    }
#endif

#if I2C_TEST
    if ((status = pbus_device_add(&hikey->pbus, &i2c_test_dev)) != ZX_OK) {
        zxlogf(ERROR, "hikey960_add_devices could not add i2c_test_dev: %d\n", status);
    }
#endif

    if ((status = pbus_device_add(&hikey->pbus, &dummy_display_dev)) != ZX_OK) {
        zxlogf(ERROR, "hikey960_add_devices could not add dummy_display_dev: %d\n", status);
    }

    return ZX_OK;
}
