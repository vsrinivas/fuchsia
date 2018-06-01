// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/i2c.h>
#include <ddk/protocol/platform-defs.h>
#include <stdlib.h>
#include <zircon/assert.h>
#include <zircon/device/rtc.h>

typedef struct {
    i2c_protocol_t i2c;
} pcf8563_context;

static uint8_t int2bcd(int x)
{
    return ((x / 10) << 4) | (x % 10);
}

static int bcd2int(uint8_t b)
{
    return (((b & 0xf0) >> 4) * 10) + (b & 0x0f);
}

static ssize_t pcf8563_rtc_get(pcf8563_context *ctx, void* buf, size_t count) {
    ZX_DEBUG_ASSERT(ctx);

    rtc_t* rtc = buf;
    if (count < sizeof *rtc) {
        return ZX_ERR_BUFFER_TOO_SMALL;
    }

    uint8_t write_buf[] = { 0x02 };
    uint8_t read_buf[7];
    zx_status_t err = i2c_transact_sync(&ctx->i2c, 0,
                                        write_buf, sizeof write_buf,
                                        read_buf, sizeof read_buf);
    if (err) {
        return err;
    }

    rtc->seconds = bcd2int(read_buf[0] & 0x7f);
    rtc->minutes = bcd2int(read_buf[1] & 0x7f);
    rtc->hours   = bcd2int(read_buf[2] & 0x3f);
    rtc->day     = bcd2int(read_buf[3] & 0x3f);
    rtc->month   = bcd2int(read_buf[5] & 0x1f);
    rtc->year    = ((read_buf[5] & 0x80) ? 2000 : 1900) + bcd2int(read_buf[6]);

    return sizeof *rtc;
}

static ssize_t pcf8563_rtc_set(pcf8563_context *ctx, const void* buf, size_t count) {
    ZX_DEBUG_ASSERT(ctx);

    const rtc_t* rtc = buf;
    if (count < sizeof *rtc) {
        return ZX_ERR_BUFFER_TOO_SMALL;
    }

    int year = rtc->year;
    int century = (year < 2000) ? 0 : 1;
    if (century) {
        year -= 2000;
    } else {
        year -= 1900;
    }

    uint8_t write_buf[] = {
        0x02,
        int2bcd(rtc->seconds),
        int2bcd(rtc->minutes),
        int2bcd(rtc->hours),
        int2bcd(rtc->day),
        0, // day of week
        (century << 7) | int2bcd(rtc->month),
        int2bcd(year)
    };

    zx_status_t err = i2c_transact_sync(&ctx->i2c, 0, write_buf, sizeof write_buf, NULL, 0);
    if (err) {
        return err;
    }

    return sizeof *rtc;
}

static zx_status_t pcf8563_rtc_ioctl(void* ctx, uint32_t op,
                                     const void* in_buf, size_t in_len,
                                     void* out_buf, size_t out_len, size_t* out_actual) {
    switch (op) {
    case IOCTL_RTC_GET: {
        ssize_t ret = pcf8563_rtc_get(ctx, out_buf, out_len);
        if (ret < 0) {
            return ret;
        }
        *out_actual = ret;
        return ZX_OK;
    }
    case IOCTL_RTC_SET: {
        ssize_t ret = pcf8563_rtc_set(ctx, in_buf, in_len);
        if (ret < 0) {
            return ret;
        }
        return ZX_OK;
    }
    }
    return ZX_ERR_NOT_SUPPORTED;
}

static zx_protocol_device_t pcf8563_rtc_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .ioctl = pcf8563_rtc_ioctl,
};

static zx_status_t pcf8563_bind(void* ctx, zx_device_t* parent)
{
    pcf8563_context* context = calloc(1, sizeof *context);
    if (!context) {
        zxlogf(ERROR, "%s: failed to create device context\n", __FUNCTION__);
        return ZX_ERR_NO_MEMORY;
    }

    zx_status_t status = device_get_protocol(parent, ZX_PROTOCOL_I2C, &context->i2c);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: failed to acquire i2c\n", __FUNCTION__);
        free(context);
        return status;
    }

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "rtc",
        .ops = &pcf8563_rtc_device_proto,
        .proto_id = ZX_PROTOCOL_RTC,
        .ctx = context
    };

    zx_device_t* dev;
    status = device_add(parent, &args, &dev);
    if (status != ZX_OK) {
        free(context);
        return status;
    }

    return ZX_OK;
}

static zx_driver_ops_t pcf8563_rtc_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = pcf8563_bind,
};

// clang-format off
ZIRCON_DRIVER_BEGIN(pcf8563_rtc, pcf8563_rtc_ops, "pcf8563_rtc", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_NXP),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_PCF8563),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_PCF8563_RTC),
ZIRCON_DRIVER_END(pcf8563_rtc)
// clang-format on
