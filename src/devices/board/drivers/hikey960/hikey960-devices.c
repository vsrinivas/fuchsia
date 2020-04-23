// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

#include <ddk/debug.h>
#include <ddk/metadata.h>
#include <ddk/metadata/display.h>
#include <ddk/platform-defs.h>
#include <soc/hi3660/hi3660-hw.h>

#include "hikey960-hw.h"
#include "hikey960.h"

// #define GPIO_TEST 1
// #define I2C_TEST 1

// Disabled until these drivers are converted to use composite device model.
#define ENABLE_DISPLAY 0

#if ENABLE_DISPLAY

static const pbus_mmio_t dsi_mmios[] = {
    {
        .base = MMIO_DSI_BASE,
        .length = MMIO_DSI_LENGTH,
    },
};

static const pbus_i2c_channel_t display_i2c_channel_list[] = {
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

static const pbus_gpio_t display_gpios[] = {
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

static const pbus_bti_t display_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_DSI,
    },
};

static const pbus_dev_t hi_display_dev[] = {
    {
        .name = "hi-display",
        .gpio_list = display_gpios,
        .gpio_count = countof(display_gpios),
        .i2c_channel_list = display_i2c_channel_list,
        .i2c_channel_count = countof(display_i2c_channel_list),
        .bti_list = display_btis,
        .bti_count = countof(display_btis),
    },
};

static const display_driver_t display_driver_info[] = {
    {
        .vid = PDEV_VID_96BOARDS,
        .pid = PDEV_PID_HIKEY960,
        .did = PDEV_DID_HI_DISPLAY,
    },
};

static const pbus_metadata_t display_metadata[] = {
    {
        .type = DEVICE_METADATA_PRIVATE,
        .data_buffer = &display_driver_info,
        .data_size = sizeof(display_driver_t),
    },
};

static pbus_dev_t dsi_dev = {
    .name = "dw-dsi",
    .vid = PDEV_VID_GENERIC,
    .pid = PDEV_PID_GENERIC,
    .did = PDEV_DID_DW_DSI,
    .metadata_list = display_metadata,
    .metadata_count = countof(display_metadata),
    .mmio_list = dsi_mmios,
    .mmio_count = countof(dsi_mmios),
    .child_list = hi_display_dev,
    .child_count = countof(hi_display_dev),
};

#endif  // ENABLE_DISPLAY

static const pbus_mmio_t ufs_mmios[] = {
    {
        .base = MMIO_UFS_CFG_BASE,
        .length = MMIO_UFS_CFG_LENGTH,
    },
    {
        .base = MMIO_UFS_SYS_CTRL_BASE,
        .length = MMIO_UFS_SYS_CTRL_LENGTH,
    },
};

static const pbus_bti_t ufs_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_UFS_DWC3,
    },
};

static const pbus_dev_t ufs_dev = {
    .name = "ufs",
    .vid = PDEV_VID_96BOARDS,
    .pid = PDEV_PID_HIKEY960,
    .did = PDEV_DID_HISILICON_UFS,
    .mmio_list = ufs_mmios,
    .mmio_count = countof(ufs_mmios),
    .bti_list = ufs_btis,
    .bti_count = countof(ufs_btis),
};

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

static const pbus_dev_t hikey960_clk_dev = {
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

zx_status_t hikey960_add_devices(hikey960_t* hikey) {
  zx_status_t status;

  if ((status = pbus_device_add(&hikey->pbus, &hikey960_clk_dev)) != ZX_OK) {
    zxlogf(ERROR, "hikey960_add_devices could not add clk_dev: %d", status);
  }
  if ((status = hikey960_usb_init(hikey)) != ZX_OK) {
    zxlogf(ERROR, "hikey960_usb_init failed: %d", status);
  }

  if ((status = pbus_device_add(&hikey->pbus, &ufs_dev)) != ZX_OK) {
    zxlogf(ERROR, "hikey960_add_devices could not add ufs_dev: %d", status);
    return status;
  }

  if ((status = pbus_device_add(&hikey->pbus, &mali_dev)) != ZX_OK) {
    zxlogf(ERROR, "hikey960_add_devices could not add mali_dev: %d", status);
    return status;
  }

#if GPIO_TEST
  if ((status = pbus_device_add(&hikey->pbus, &gpio_test_dev)) != ZX_OK) {
    zxlogf(ERROR, "hikey960_add_devices could not add gpio_test_dev: %d", status);
  }
#endif

#if I2C_TEST
  if ((status = pbus_device_add(&hikey->pbus, &i2c_test_dev)) != ZX_OK) {
    zxlogf(ERROR, "hikey960_add_devices could not add i2c_test_dev: %d", status);
  }
#endif

#if ENABLE_DISPLAY
  if ((status = pbus_device_add(&hikey->pbus, &dsi_dev)) != ZX_OK) {
    zxlogf(ERROR, "hikey960_add_devices could not add hi_display_dev: %d", status);
  }
#endif

  return ZX_OK;
}
