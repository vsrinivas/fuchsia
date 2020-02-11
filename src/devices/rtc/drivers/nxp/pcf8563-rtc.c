// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/device-protocol/i2c.h>
#include <librtc.h>
#include <stdlib.h>
#include <zircon/assert.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/i2c.h>

typedef struct {
  i2c_protocol_t i2c;
} pcf8563_context;

static zx_status_t set_utc_offset(const fuchsia_hardware_rtc_Time* rtc) {
  uint64_t rtc_nanoseconds = seconds_since_epoch(rtc) * 1000000000;
  int64_t offset = rtc_nanoseconds - zx_clock_get_monotonic();
  // Please do not use get_root_resource() in new code. See ZX-1467.
  return zx_clock_adjust(get_root_resource(), ZX_CLOCK_UTC, offset);
}

static zx_status_t pcf8563_rtc_get(void* ctx, fuchsia_hardware_rtc_Time* rtc) {
  ZX_DEBUG_ASSERT(ctx);

  pcf8563_context* context = ctx;
  uint8_t write_buf[] = {0x02};
  uint8_t read_buf[7];
  zx_status_t err =
      i2c_write_read_sync(&context->i2c, write_buf, sizeof write_buf, read_buf, sizeof read_buf);
  if (err) {
    return err;
  }

  rtc->seconds = from_bcd(read_buf[0] & 0x7f);
  rtc->minutes = from_bcd(read_buf[1] & 0x7f);
  rtc->hours = from_bcd(read_buf[2] & 0x3f);
  rtc->day = from_bcd(read_buf[3] & 0x3f);
  rtc->month = from_bcd(read_buf[5] & 0x1f);
  rtc->year = ((read_buf[5] & 0x80) ? 2000 : 1900) + from_bcd(read_buf[6]);

  return ZX_OK;
}

static zx_status_t pcf8563_rtc_set(void* ctx, const fuchsia_hardware_rtc_Time* rtc) {
  ZX_DEBUG_ASSERT(ctx);

  // An invalid time was supplied.
  if (rtc_is_invalid(rtc)) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  int year = rtc->year;
  int century = (year < 2000) ? 0 : 1;
  if (century) {
    year -= 2000;
  } else {
    year -= 1900;
  }

  uint8_t write_buf[] = {0x02,
                         to_bcd(rtc->seconds),
                         to_bcd(rtc->minutes),
                         to_bcd(rtc->hours),
                         to_bcd(rtc->day),
                         0,  // day of week
                         (century << 7) | to_bcd(rtc->month),
                         to_bcd(year)};

  pcf8563_context* context = ctx;
  zx_status_t err = i2c_write_read_sync(&context->i2c, write_buf, sizeof write_buf, NULL, 0);
  if (err) {
    return err;
  }

  zx_status_t status = set_utc_offset(rtc);
  if (status != ZX_OK) {
    zxlogf(ERROR, "The RTC driver was unable to set the UTC clock!\n");
  }

  return ZX_OK;
}

static zx_status_t fidl_Get(void* ctx, fidl_txn_t* txn) {
  fuchsia_hardware_rtc_Time rtc;
  pcf8563_rtc_get(ctx, &rtc);
  return fuchsia_hardware_rtc_DeviceGet_reply(txn, &rtc);
}

static zx_status_t fidl_Set(void* ctx, const fuchsia_hardware_rtc_Time* rtc, fidl_txn_t* txn) {
  zx_status_t status = pcf8563_rtc_set(ctx, rtc);
  return fuchsia_hardware_rtc_DeviceSet_reply(txn, status);
}

static fuchsia_hardware_rtc_Device_ops_t fidl_ops = {
    .Get = fidl_Get,
    .Set = fidl_Set,
};

static zx_status_t pcf8563_rtc_message(void* ctx, fidl_msg_t* msg, fidl_txn_t* txn) {
  return fuchsia_hardware_rtc_Device_dispatch(ctx, txn, msg, &fidl_ops);
}

static zx_protocol_device_t pcf8563_rtc_device_proto = {.version = DEVICE_OPS_VERSION,
                                                        .message = pcf8563_rtc_message};

static zx_status_t pcf8563_bind(void* ctx, zx_device_t* parent) {
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

  device_add_args_t args = {.version = DEVICE_ADD_ARGS_VERSION,
                            .name = "rtc",
                            .ops = &pcf8563_rtc_device_proto,
                            .proto_id = ZX_PROTOCOL_RTC,
                            .ctx = context};

  zx_device_t* dev;
  status = device_add(parent, &args, &dev);
  if (status != ZX_OK) {
    free(context);
    return status;
  }

  fuchsia_hardware_rtc_Time rtc;
  sanitize_rtc(context, &rtc, pcf8563_rtc_get, pcf8563_rtc_set);
  status = set_utc_offset(&rtc);
  if (status != ZX_OK) {
    zxlogf(ERROR, "The RTC driver was unable to set the UTC clock!\n");
  }

  return ZX_OK;
}

static zx_driver_ops_t pcf8563_rtc_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = pcf8563_bind,
};

// clang-format off
ZIRCON_DRIVER_BEGIN(pcf8563_rtc, pcf8563_rtc_ops, "pcf8563_rtc", "0.1", 2)
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_NXP),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_PCF8563_RTC),
ZIRCON_DRIVER_END(pcf8563_rtc)
    // clang-format on
