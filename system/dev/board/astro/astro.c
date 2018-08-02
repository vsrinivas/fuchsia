// Copyright 2018 The Fuchsia Authors. All rights reserved.
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
#include <hw/reg.h>

#include <soc/aml-s905d2/s905d2-hw.h>
#include <soc/aml-s905d2/aml-mali.h>

#include <zircon/assert.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/threads.h>

#include "astro.h"

static void aml_bus_release(void* ctx) {
    aml_bus_t* bus = ctx;
    free(bus);
}

static zx_protocol_device_t aml_bus_device_protocol = {
    .version = DEVICE_OPS_VERSION,
    .release = aml_bus_release,
};

static pbus_mmio_t astro_video_mmios[] = {
    {
        .base =     S905D2_CBUS_BASE,
        .length =   S905D2_CBUS_LENGTH,
    },
    {
        .base =     S905D2_DOS_BASE,
        .length =   S905D2_DOS_LENGTH,
    },
    {
        .base =     S905D2_HIU_BASE,
        .length =   S905D2_HIU_LENGTH,
    },
    {
        .base =     S905D2_AOBUS_BASE,
        .length =   S905D2_AOBUS_LENGTH,
    },
    {
        .base =     S905D2_DMC_BASE,
        .length =   S905D2_DMC_LENGTH,
    },
};

static const pbus_bti_t astro_video_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_VIDEO,
    },
};

static const pbus_irq_t astro_video_irqs[] = {
    {
        .irq = S905D2_DEMUX_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = S905D2_PARSER_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = S905D2_DOS_MBOX_0_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = S905D2_DOS_MBOX_1_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = S905D2_DOS_MBOX_2_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};

static const pbus_dev_t video_dev = {
    .name = "video",
    .vid = PDEV_VID_AMLOGIC,
    .pid = PDEV_PID_AMLOGIC_S905D2,
    .did = PDEV_DID_AMLOGIC_VIDEO,
    .mmios = astro_video_mmios,
    .mmio_count = countof(astro_video_mmios),
    .btis = astro_video_btis,
    .bti_count = countof(astro_video_btis),
    .irqs = astro_video_irqs,
    .irq_count = countof(astro_video_irqs),
};

static const pbus_dev_t rtc_dev = {
    .name = "rtc",
    .vid = PDEV_VID_GENERIC,
    .pid = PDEV_PID_GENERIC,
    .did = PDEV_DID_RTC_FALLBACK,
};

static int aml_start_thread(void* arg) {
    aml_bus_t* bus = arg;
    zx_status_t status;

    if ((status = aml_gpio_init(bus)) != ZX_OK) {
        zxlogf(ERROR, "aml_gpio_init failed: %d\n", status);
        goto fail;
    }

    if ((status = aml_i2c_init(bus)) != ZX_OK) {
        zxlogf(ERROR, "aml_i2c_init failed: %d\n", status);
        goto fail;
    }

    status = aml_mali_init(&bus->pbus, BTI_MALI);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml_mali_init failed: %d\n", status);
        goto fail;
    }

    if ((status = aml_usb_init(bus)) != ZX_OK) {
        zxlogf(ERROR, "aml_usb_init failed: %d\n", status);
        goto fail;
    }

    if ((status = astro_touch_init(bus)) != ZX_OK) {
        zxlogf(ERROR, "astro_touch_init failed: %d\n", status);
        goto fail;
    }

    if ((status = aml_display_init(bus)) != ZX_OK) {
        zxlogf(ERROR, "aml_display_init failed: %d\n", status);
        goto fail;
    }

    if ((status = aml_canvas_init(bus)) != ZX_OK) {
        zxlogf(ERROR, "aml_canvas_init failed: %d\n", status);
        goto fail;
    }

    if ((status = pbus_device_add(&bus->pbus, &video_dev, 0)) != ZX_OK) {
        zxlogf(ERROR, "aml_start_thread could not add video_dev: %d\n", status);
        goto fail;
    }

    if ((status = pbus_device_add(&bus->pbus, &rtc_dev, 0)) != ZX_OK) {
        zxlogf(ERROR, "aml_start_thread could not add rtc_dev: %d\n", status);
        goto fail;
    }

    if ((status = aml_raw_nand_init(bus)) != ZX_OK) {
        zxlogf(ERROR, "aml_raw_nand_init failed: %d\n", status);
        goto fail;
    }

    if ((status = aml_sdio_init(bus)) != ZX_OK) {
        zxlogf(ERROR, "aml_sdio_init failed: %d\n", status);
        goto fail;
    }

    if ((status = ams_light_init(bus)) != ZX_OK) {
        zxlogf(ERROR, "ams_light_init failed: %d\n", status);
        goto fail;
    }

    // This function includes some non-trivial delays, so lets run this last
    // to avoid slowing down the rest of the boot.
    if ((status = aml_bluetooth_init(bus)) != ZX_OK) {
        zxlogf(ERROR, "aml_bluetooth_init failed: %d\n", status);
        goto fail;
    }

    if ((status = aml_thermal_init(bus)) != ZX_OK) {
        zxlogf(ERROR, "aml_thermal_init failed: %d\n", status);
        goto fail;
    }

    return ZX_OK;
fail:
    zxlogf(ERROR, "aml_start_thread failed, not all devices have been initialized\n");
    return status;
}

static zx_status_t aml_bus_bind(void* ctx, zx_device_t* parent) {
    aml_bus_t* bus = calloc(1, sizeof(aml_bus_t));
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
        zxlogf(ERROR, "aml_bus_bind: could not get ZX_PROTOCOL_IOMMU\n");
        goto fail;
    }

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "aml-bus",
        .ctx = bus,
        .ops = &aml_bus_device_protocol,
        .flags = DEVICE_ADD_NON_BINDABLE,
    };

    status = device_add(parent, &args, NULL);
    if (status != ZX_OK) {
        goto fail;
    }

    thrd_t t;
    int thrd_rc = thrd_create_with_name(&t, aml_start_thread, bus, "aml_start_thread");
    if (thrd_rc != thrd_success) {
        status = thrd_status_to_zx_status(thrd_rc);
        goto fail;
    }
    return ZX_OK;

fail:
    zxlogf(ERROR, "aml_bus_bind failed %d\n", status);
    aml_bus_release(bus);
    return status;
}

static zx_driver_ops_t aml_bus_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = aml_bus_bind,
};

ZIRCON_DRIVER_BEGIN(aml_bus, aml_bus_driver_ops, "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PLATFORM_BUS),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_GOOGLE),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_ASTRO),
ZIRCON_DRIVER_END(aml_bus)
