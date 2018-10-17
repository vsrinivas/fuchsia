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
#include <librtc.h>

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

static zx_status_t set_utc_offset(const rtc_t* rtc) {
    uint64_t rtc_nanoseconds = seconds_since_epoch(rtc) * 1000000000;
    int64_t offset = rtc_nanoseconds - zx_clock_get_monotonic();
    return zx_clock_adjust(get_root_resource(), ZX_CLOCK_UTC, offset);
}

static ssize_t pl031_rtc_get(pl031_t *ctx, void* buf, size_t count) {
    ZX_DEBUG_ASSERT(ctx);

    rtc_t* rtc = buf;
    if (count < sizeof *rtc) {
        return ZX_ERR_BUFFER_TOO_SMALL;
    }

    seconds_to_rtc(ctx->regs->dr, rtc);

    return sizeof *rtc;
}

static ssize_t pl031_rtc_set(pl031_t *ctx, const void* buf, size_t count) {
    ZX_DEBUG_ASSERT(ctx);

    const rtc_t* rtc = buf;
    if (count < sizeof *rtc) {
        return ZX_ERR_BUFFER_TOO_SMALL;
    }

    // An invalid time was supplied.
    if (rtc_is_invalid(rtc)) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    ctx->regs->lr = seconds_since_epoch(rtc);

    zx_status_t status = set_utc_offset(rtc);
    if (status != ZX_OK) {
        zxlogf(ERROR, "The RTC driver was unable to set the UTC clock!\n");
    }

    return sizeof *rtc;
}

static zx_status_t pl031_rtc_ioctl(void* ctx, uint32_t op,
                                   const void* in_buf, size_t in_len,
                                   void* out_buf, size_t out_len, size_t* out_actual) {
    switch (op) {
    case IOCTL_RTC_GET: {
        ssize_t ret = pl031_rtc_get(ctx, out_buf, out_len);
        if (ret < 0) {
            return ret;
        }
        *out_actual = ret;
        return ZX_OK;
    }
    case IOCTL_RTC_SET:
        return pl031_rtc_set(ctx, in_buf, in_len);
    }
    return ZX_ERR_NOT_SUPPORTED;
}

static zx_protocol_device_t pl031_rtc_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .ioctl = pl031_rtc_ioctl,
};

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
        .ctx = pl031
    };

    zx_device_t* dev;
    st = device_add(parent, &args, &dev);
    if (st != ZX_OK) {
        zxlogf(ERROR, "pl031_rtc: error adding device\n");
        goto error_return;
    }

    // set the current RTC offset in the kernel
    rtc_t rtc;
    sanitize_rtc(pl031, &pl031_rtc_device_proto, &rtc);
    st = set_utc_offset(&rtc);
    if (st != ZX_OK) {
        zxlogf(ERROR, "pl031_rtc: unable to set the UTC clock!\n");
    }

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
