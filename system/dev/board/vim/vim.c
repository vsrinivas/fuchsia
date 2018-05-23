// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/platform-defs.h>
#include <ddk/protocol/scpi.h>
#include <hw/reg.h>

#include <soc/aml-s912/s912-hw.h>
#include <soc/aml-s912/s912-gpio.h>

#include <zircon/assert.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/threads.h>

#include "vim.h"

// DMC MMIO for display driver
static pbus_mmio_t vim_display_mmios[] = {
    {
        .base =     S912_PRESET_BASE,
        .length =   S912_PRESET_LENGTH,
    },
    {
        .base =     S912_HDMITX_BASE,
        .length =   S912_HDMITX_LENGTH,
    },
    {
        .base =     S912_HIU_BASE,
        .length =   S912_HIU_LENGTH,
    },
    {
        .base =     S912_VPU_BASE,
        .length =   S912_VPU_LENGTH,
    },
    {
        .base =     S912_HDMITX_SEC_BASE,
        .length =   S912_HDMITX_SEC_LENGTH,
    },
    {
        .base =     S912_DMC_REG_BASE,
        .length =   S912_DMC_REG_LENGTH,
    },
    {
        .base =     S912_CBUS_REG_BASE,
        .length =   S912_CBUS_REG_LENGTH,
    },
};

const pbus_gpio_t vim_display_gpios[] = {
    {
        // HPD
        .gpio = S912_GPIOH(0),
    },
};

static const pbus_irq_t vim_display_irqs[] = {
    {
        .irq = S912_VIU1_VSYNC_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};

static const pbus_bti_t vim_display_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_DISPLAY,
    },
};

static const pbus_dev_t display_dev = {
    .name = "display",
    .vid = PDEV_VID_KHADAS,
    .pid = PDEV_PID_VIM2,
    .did = PDEV_DID_VIM_DISPLAY,
    .mmios = vim_display_mmios,
    .mmio_count = countof(vim_display_mmios),
    .gpios = vim_display_gpios,
    .gpio_count = countof(vim_display_gpios),
    .irqs = vim_display_irqs,
    .irq_count = countof(vim_display_irqs),
    .btis = vim_display_btis,
    .bti_count = countof(vim_display_btis),
};

static void vim_bus_release(void* ctx) {
    vim_bus_t* bus = ctx;
    free(bus);
}

static zx_protocol_device_t vim_bus_device_protocol = {
    .version = DEVICE_OPS_VERSION,
    .release = vim_bus_release,
};

static const pbus_i2c_channel_t led2472g_channels[] = {
  {
    .bus_id = 0,
    .address = 0x46,
  },
};

static const pbus_dev_t led2472g_dev = {
  .name = "led2472g",
  .vid = PDEV_VID_GENERIC,
  .pid = PDEV_PID_GENERIC,
  .did = PDEV_DID_LED2472G,
  .i2c_channels = led2472g_channels,
  .i2c_channel_count = countof(led2472g_channels),
};

static pbus_mmio_t vim_video_mmios[] = {
    {
        .base =     S912_FULL_CBUS_BASE,
        .length =   S912_FULL_CBUS_LENGTH,
    },
    {
        .base =     S912_DOS_BASE,
        .length =   S912_DOS_LENGTH,
    },
    {
        .base =     S912_HIU_BASE,
        .length =   S912_HIU_LENGTH,
    },
    {
        .base =     S912_AOBUS_BASE,
        .length =   S912_AOBUS_LENGTH,
    },
    {
        .base =     S912_DMC_REG_BASE,
        .length =   S912_DMC_REG_LENGTH,
    },
};

static const pbus_bti_t vim_video_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_VIDEO,
    },
};

static const pbus_irq_t vim_video_irqs[] = {
    {
        .irq = S912_DEMUX_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = S912_PARSER_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = S912_DOS_MBOX_0_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = S912_DOS_MBOX_1_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = S912_DOS_MBOX_2_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};

static const pbus_dev_t video_dev = {
    .name = "video",
    .vid = PDEV_VID_AMLOGIC,
    .pid = PDEV_PID_AMLOGIC_S912,
    .did = PDEV_DID_AMLOGIC_VIDEO,
    .mmios = vim_video_mmios,
    .mmio_count = countof(vim_video_mmios),
    .btis = vim_video_btis,
    .bti_count = countof(vim_video_btis),
    .irqs = vim_video_irqs,
    .irq_count = countof(vim_video_irqs),
};

static zx_status_t vim_limit_cpu_freq(vim_bus_t* bus) {
    scpi_protocol_t scpi;
    scpi_opp_t opps;

    zx_status_t status = pbus_wait_protocol(&bus->pbus, ZX_PROTOCOL_SCPI);
    if (status != ZX_OK) {
        zxlogf(ERROR, "vim_limit_cpu_freq: pbus_wait_protocol failed: %d\n", status);
        return status;
    }

    status = device_get_protocol(bus->parent, ZX_PROTOCOL_SCPI, &scpi);
    if (status != ZX_OK) {
        zxlogf(ERROR, "vim_limit_cpu_freq: failed to get ZX_PROTOCOL_SCPI %d \n", status);
        return status;
    }

    status = scpi_get_dvfs_info(&scpi, BIG_CLUSTER_POWER_DOMAIN, &opps);
    if (status != ZX_OK) {
        zxlogf(ERROR, "vim_limit_cpu_freq: scpi_get_dvfs_info for Big cluster"
               "failed %d\n", status);
        return status;
    }

    // Set the OP index for Big cluster
    for (uint32_t i=0; i<opps.count; i++) {
        if (opps.opp[i].freq_hz == BIG_CLUSTER_CPU_FREQ_MAX) {
            status = scpi_set_dvfs_idx(&scpi, BIG_CLUSTER_POWER_DOMAIN, i);
            if (status != ZX_OK) {
                zxlogf(ERROR, "vim_limit_cpu_freq: scpi_set_dvfs_idx for"
                       "Big Cluster failed %d\n", status);
                return status;
            }
            break;
        }
    }

    status = scpi_get_dvfs_info(&scpi, LITTLE_CLUSTER_POWER_DOMAIN, &opps);
    if (status != ZX_OK) {
        zxlogf(ERROR, "vim_limit_cpu_freq: scpi_get_dvfs_info for Little cluster"
               "failed %d\n", status);
        return status;
    }

    // Set the OP index for Little cluster
    for (uint32_t i=0; i<opps.count; i++) {
        if (opps.opp[i].freq_hz == LITTLE_CLUSTER_CPU_FREQ_MAX) {
            status = scpi_set_dvfs_idx(&scpi, LITTLE_CLUSTER_POWER_DOMAIN, i);
            if (status != ZX_OK) {
                zxlogf(ERROR, "vim_limit_cpu_freq: scpi_set_dvfs_idx for"
                       "Little cluster failed %d\n", status);
                return status;
            }
            break;
        }
    }
    return ZX_OK;
}

static int vim_start_thread(void* arg) {
    vim_bus_t* bus = arg;
    zx_status_t status;

    if ((status = vim_gpio_init(bus)) != ZX_OK) {
        zxlogf(ERROR, "vim_gpio_init failed: %d\n", status);
        goto fail;
    }
    if ((status = vim_i2c_init(bus)) != ZX_OK) {
        zxlogf(ERROR, "vim_i2c_init failed: %d\n", status);
        goto fail;
    }
    if ((status = vim_uart_init(bus)) != ZX_OK) {
        zxlogf(ERROR, "vim_uart_init failed: %d\n", status);
        goto fail;
    }
    if ((status = vim_usb_init(bus)) != ZX_OK) {
        zxlogf(ERROR, "vim_usb_init failed: %d\n", status);
        goto fail;
    }

    if ((status = vim_mali_init(bus)) != ZX_OK) {
        zxlogf(ERROR, "vim_mali_init failed: %d\n", status);
        goto fail;
    }

    if ((status = vim_sd_emmc_init(bus)) != ZX_OK) {
        zxlogf(ERROR, "vim_sd_emmc_init failed: %d\n", status);
        goto fail;
    }

    if ((status = vim2_mailbox_init(bus)) != ZX_OK) {
        zxlogf(ERROR, "vim2_mailbox_init failed: %d\n", status);
        goto fail;
    }
    if ((status = vim2_thermal_init(bus)) != ZX_OK) {
        zxlogf(ERROR, "vim2_thermal_init failed: %d\n", status);
        goto fail;
    }

    if ((status = pbus_device_add(&bus->pbus, &display_dev, 0)) != ZX_OK) {
        zxlogf(ERROR, "vim_start_thread could not add display_dev: %d\n", status);
        goto fail;
    }

    if ((status = pbus_device_add(&bus->pbus, &video_dev, 0)) != ZX_OK) {
      zxlogf(ERROR, "vim_start_thread could not add video_dev: %d\n", status);
      goto fail;
    }

    if ((status = pbus_device_add(&bus->pbus, &led2472g_dev, 0)) != ZX_OK) {
      zxlogf(ERROR, "vim_start_thread could not add led2472g_dev: %d\n", status);
      goto fail;
    }

    if ((status = vim_eth_init(bus)) != ZX_OK) {
        zxlogf(ERROR, "vim_eth_init failed: %d\n", status);
        goto fail;
    }

    // VIM2 is known to be unstable above 1.29Ghx
    // Setting the default CPU frequency to be that
    // TODO(braval): Replace this with a userspace app which
    // has a device specific policy with min-max freq limits
    // and regulates the frequency based on the temperature
    if ((status = vim_limit_cpu_freq(bus)) != ZX_OK) {
        zxlogf(ERROR, "vim_limit_cpu_freq failed: %d\n", status);
        goto fail;
    }

    return ZX_OK;
fail:
    zxlogf(ERROR, "vim_start_thread failed, not all devices have been initialized\n");
    return status;
}

static zx_status_t vim_bus_bind(void* ctx, zx_device_t* parent) {
    vim_bus_t* bus = calloc(1, sizeof(vim_bus_t));
    if (!bus) {
        return ZX_ERR_NO_MEMORY;
    }
    bus->parent = parent;

    zx_status_t status = device_get_protocol(parent, ZX_PROTOCOL_PLATFORM_BUS, &bus->pbus);
    if (status != ZX_OK) {
        goto fail;
    }

    // get default BTI from the dummy IOMMU implementation in the platform bus
    status = device_get_protocol(parent, ZX_PROTOCOL_IOMMU, &bus->iommu);
    if (status != ZX_OK) {
        zxlogf(ERROR, "vim_bus_bind: could not get ZX_PROTOCOL_IOMMU\n");
        goto fail;
    }

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "vim-bus",
        .ctx = bus,
        .ops = &vim_bus_device_protocol,
        .flags = DEVICE_ADD_NON_BINDABLE,
    };

    status = device_add(parent, &args, NULL);
    if (status != ZX_OK) {
        goto fail;
    }

    thrd_t t;
    int thrd_rc = thrd_create_with_name(&t, vim_start_thread, bus, "vim_start_thread");
    if (thrd_rc != thrd_success) {
        status = thrd_status_to_zx_status(thrd_rc);
        goto fail;
    }

    return ZX_OK;

fail:
    zxlogf(ERROR, "vim_bus_bind failed %d\n", status);
    vim_bus_release(bus);
    return status;
}

static zx_driver_ops_t vim_bus_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = vim_bus_bind,
};

ZIRCON_DRIVER_BEGIN(vim_bus, vim_bus_driver_ops, "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PLATFORM_BUS),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_KHADAS),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_VIM2),
ZIRCON_DRIVER_END(vim_bus)
