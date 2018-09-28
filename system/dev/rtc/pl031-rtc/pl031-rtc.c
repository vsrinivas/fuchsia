// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform-device.h>
#include <zircon/device/rtc.h>

#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct pl031_regs {
    volatile uint32_t dr;
    volatile uint32_t mr;
    volatile uint32_t lr;
    volatile uint32_t cr;
    volatile uint32_t msc;
    volatile uint32_t ris;
    volatile uint32_t mis;
    volatile uint32_t icr;
} pl031_regs_t;

typedef struct pl031 {
    zx_device_t* parent;
    mmio_buffer_t mmio;
    pl031_regs_t* regs;
} pl031_t;

static zx_protocol_device_t pl031_rtc_device_proto = {
    .version = DEVICE_OPS_VERSION,
};

static void pl031_set_kernel_offset(pl031_t* pl031) {
    // the data register seems to hold second offsets
    uint32_t offset32;
    uint32_t last = 0;

    // read the value until it rolls, then latch that
    last = pl031->regs->dr;
    do {
        offset32 = pl031->regs->dr;
    } while (offset32 != 0 && offset32 == last);

    if (offset32 == 0) {
        zxlogf(ERROR, "pl031_rtc: zero read from DR, aborting\n");
        return;
    }

    int64_t offset = ZX_SEC(offset32);
    zx_status_t status = zx_clock_adjust(get_root_resource(), ZX_CLOCK_UTC, offset);
    if (status != ZX_OK) {
        zxlogf(ERROR, "The RTC driver was unable to set the UTC clock!\n");
    }
}

static zx_status_t pl031_rtc_bind(void* ctx, zx_device_t* parent) {
    zxlogf(TRACE, "pl031_rtc: bind parent = %p\n", parent);

    platform_device_protocol_t proto;
    zx_status_t st = device_get_protocol(parent, ZX_PROTOCOL_PLATFORM_DEV, &proto);
    if (st != ZX_OK) {
        return st;
    }

    // Allocate a new device object for the bus.
    pl031_t* pl031 = calloc(1, sizeof(*pl031));
    if (!pl031) {
        zxlogf(ERROR, "pl031_rtc: bind failed to allocate pl031_t struct\n");
        return ZX_ERR_NO_MEMORY;
    }

    // Carve out some address space for this device.
    st = pdev_map_mmio_buffer2(&proto, 0, ZX_CACHE_POLICY_UNCACHED_DEVICE, &pl031->mmio);
    if (st != ZX_OK) {
        zxlogf(ERROR, "pl031_rtc: bind failed to pdev_map_mmio.\n");
        goto error_return;
    }
    pl031->regs = pl031->mmio.vaddr;

    pl031->parent = parent;

    // bind the device
    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "rtc",
        .proto_id = ZX_PROTOCOL_RTC,
        .ops = &pl031_rtc_device_proto,
    };

    zx_device_t* dev;
    st = device_add(parent, &args, &dev);
    if (st != ZX_OK) {
        zxlogf(ERROR, "pl031_rtc: error adding device\n");
        goto error_return;
    }

    // set the current RTC offset in the kernel
    pl031_set_kernel_offset(pl031);

    return ZX_OK;

error_return:
    if (pl031) {
        mmio_buffer_release(&pl031->mmio);
        free(pl031);
    }

    return st;
}

static void pl031_rtc_release(void* ctx) {
    pl031_t* pl031 = (pl031_t*)ctx;
    mmio_buffer_release(&pl031->mmio);
    free(pl031);
}

static zx_driver_ops_t pl031_rtc_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = pl031_rtc_bind,
    .release = pl031_rtc_release,
};

// The formatter does not play nice with these macros.
// clang-format off
ZIRCON_DRIVER_BEGIN(pl031, pl031_rtc_driver_ops, "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_GENERIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_GENERIC),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_RTC_PL031),
ZIRCON_DRIVER_END(pl031)
    // clang-format on
