// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/device-protocol/platform-device.h>
#include <librtc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/mmio-buffer.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/device.h>

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
  MMIO_PTR pl031_regs_t* regs;
} pl031_t;

static zx_status_t set_utc_offset(const fuchsia_hardware_rtc_Time* rtc) {
  uint64_t rtc_nanoseconds = seconds_since_epoch(rtc) * 1000000000;
  int64_t offset = rtc_nanoseconds - zx_clock_get_monotonic();
  // Please do not use get_root_resource() in new code. See fxbug.dev/31358.
  return zx_clock_adjust(get_root_resource(), ZX_CLOCK_UTC, offset);
}

static zx_status_t pl031_rtc_get(void* ctx, fuchsia_hardware_rtc_Time* rtc) {
  ZX_DEBUG_ASSERT(ctx);
  pl031_t* context = ctx;
  seconds_to_rtc(MmioRead32(&context->regs->dr), rtc);
  return ZX_OK;
}

static zx_status_t pl031_rtc_set(void* ctx, const fuchsia_hardware_rtc_Time* rtc) {
  ZX_DEBUG_ASSERT(ctx);

  // An invalid time was supplied.
  if (rtc_is_invalid(rtc)) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  pl031_t* context = ctx;
  MmioWrite32(seconds_since_epoch(rtc), &context->regs->lr);

  zx_status_t status = set_utc_offset(rtc);
  if (status != ZX_OK) {
    zxlogf(ERROR, "The RTC driver was unable to set the UTC clock!");
  }

  return ZX_OK;
}

static zx_status_t fidl_Get(void* ctx, fidl_txn_t* txn) {
  fuchsia_hardware_rtc_Time rtc;
  pl031_rtc_get(ctx, &rtc);
  return fuchsia_hardware_rtc_DeviceGet_reply(txn, &rtc);
}

static zx_status_t fidl_Set(void* ctx, const fuchsia_hardware_rtc_Time* rtc, fidl_txn_t* txn) {
  zx_status_t status = pl031_rtc_set(ctx, rtc);
  return fuchsia_hardware_rtc_DeviceSet_reply(txn, status);
}

static fuchsia_hardware_rtc_Device_ops_t fidl_ops = {
    .Get = fidl_Get,
    .Set = fidl_Set,
};

static zx_status_t pl031_rtc_message(void* ctx, fidl_msg_t* msg, fidl_txn_t* txn) {
  return fuchsia_hardware_rtc_Device_dispatch(ctx, txn, msg, &fidl_ops);
}

static zx_protocol_device_t pl031_rtc_device_proto = {.version = DEVICE_OPS_VERSION,
                                                      .message = pl031_rtc_message};

static zx_status_t pl031_rtc_bind(void* ctx, zx_device_t* parent) {
  zxlogf(DEBUG, "pl031_rtc: bind parent = %p", parent);

  pdev_protocol_t proto;
  zx_status_t st = device_get_protocol(parent, ZX_PROTOCOL_PDEV, &proto);
  if (st != ZX_OK) {
    return st;
  }

  // Allocate a new device object for the bus.
  pl031_t* pl031 = calloc(1, sizeof(*pl031));
  if (!pl031) {
    zxlogf(ERROR, "pl031_rtc: bind failed to allocate pl031_t struct");
    return ZX_ERR_NO_MEMORY;
  }

  // Carve out some address space for this device.
  st = pdev_map_mmio_buffer(&proto, 0, ZX_CACHE_POLICY_UNCACHED_DEVICE, &pl031->mmio);
  if (st != ZX_OK) {
    zxlogf(ERROR, "pl031_rtc: bind failed to pdev_map_mmio.");
    goto error_return;
  }
  pl031->regs = pl031->mmio.vaddr;

  pl031->parent = parent;

  // bind the device
  device_add_args_t args = {.version = DEVICE_ADD_ARGS_VERSION,
                            .name = "rtc",
                            .proto_id = ZX_PROTOCOL_RTC,
                            .ops = &pl031_rtc_device_proto,
                            .ctx = pl031};

  zx_device_t* dev;
  st = device_add(parent, &args, &dev);
  if (st != ZX_OK) {
    zxlogf(ERROR, "pl031_rtc: error adding device");
    goto error_return;
  }

  // set the current RTC offset in the kernel
  fuchsia_hardware_rtc_Time rtc;
  sanitize_rtc(pl031, &rtc, pl031_rtc_get, pl031_rtc_set);
  st = set_utc_offset(&rtc);
  if (st != ZX_OK) {
    zxlogf(ERROR, "pl031_rtc: unable to set the UTC clock!");
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
