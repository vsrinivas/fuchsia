// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/sysinfo/c/fidl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <zircon/boot/image.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/syscalls/resource.h>
#include <zircon/types.h>

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/metadata.h>

typedef struct {
  zx_device_t* zxdev;
  mtx_t lock;
  char board_name[ZBI_BOARD_NAME_LEN];
  uint32_t board_revision;
} sysinfo_t;

static zx_status_t fidl_get_hypervisor_resource(void* ctx, fidl_txn_t* txn) {
  zx_handle_t h;
  const char name[] = "hypervisor";
  // Please do not use get_root_resource() in new code. See ZX-1467.
  zx_status_t status = zx_resource_create(get_root_resource(), ZX_RSRC_KIND_HYPERVISOR, 0, 0, name,
                                          sizeof(name), &h);
  return fuchsia_sysinfo_DeviceGetHypervisorResource_reply(txn, status, h);
}

static zx_status_t fidl_get_board_name(void* ctx, fidl_txn_t* txn) {
  sysinfo_t* sysinfo = ctx;

  zx_status_t status = ZX_OK;

  mtx_lock(&sysinfo->lock);
  if (sysinfo->board_name[0] == 0) {
    size_t actual = 0;
    status = device_get_metadata(sysinfo->zxdev, DEVICE_METADATA_BOARD_NAME, sysinfo->board_name,
                                 sizeof(sysinfo->board_name), &actual);
  }
  mtx_unlock(&sysinfo->lock);

  size_t board_name_len = strnlen(sysinfo->board_name, sizeof(sysinfo->board_name));
  return fuchsia_sysinfo_DeviceGetBoardName_reply(txn, status, sysinfo->board_name, board_name_len);
}

static zx_status_t fidl_get_board_revision(void* ctx, fidl_txn_t* txn) {
  sysinfo_t* sysinfo = ctx;

  zx_status_t status = ZX_OK;

  mtx_lock(&sysinfo->lock);
  if (sysinfo->board_revision == 0) {
    size_t actual = 0;
    status = device_get_metadata(sysinfo->zxdev, DEVICE_METADATA_BOARD_REVISION,
                                 &sysinfo->board_revision,
                                 sizeof(sysinfo->board_revision), &actual);
  }
  mtx_unlock(&sysinfo->lock);

  return fuchsia_sysinfo_DeviceGetBoardRevision_reply(txn, status, sysinfo->board_revision);
}

static zx_status_t fidl_get_interrupt_controller_info(void* ctx, fidl_txn_t* txn) {
  zx_status_t status = ZX_OK;
  fuchsia_sysinfo_InterruptControllerInfo info = {};

#if defined(__aarch64__)
  sysinfo_t* sysinfo = ctx;
  size_t actual = 0;
  status = device_get_metadata(sysinfo->zxdev, DEVICE_METADATA_INTERRUPT_CONTROLLER_TYPE,
                               &info.type, sizeof(uint8_t), &actual);
#elif defined(__x86_64__)
  info.type = fuchsia_sysinfo_InterruptControllerType_APIC;
#else
  info.type = fuchsia_sysinfo_InterruptControllerType_UNKNOWN;
#endif

  return fuchsia_sysinfo_DeviceGetInterruptControllerInfo_reply(txn, status, &info);
}

static fuchsia_sysinfo_Device_ops_t fidl_ops = {
    .GetHypervisorResource = fidl_get_hypervisor_resource,
    .GetBoardName = fidl_get_board_name,
    .GetBoardRevision = fidl_get_board_revision,
    .GetInterruptControllerInfo = fidl_get_interrupt_controller_info,
};

static zx_status_t sysinfo_message(void* ctx, fidl_msg_t* msg, fidl_txn_t* txn) {
  return fuchsia_sysinfo_Device_dispatch(ctx, txn, msg, &fidl_ops);
}

static zx_protocol_device_t sysinfo_ops = {
    .version = DEVICE_OPS_VERSION,
    .message = sysinfo_message,
};

zx_status_t sysinfo_bind(void* ctx, zx_device_t* parent) {
  sysinfo_t* sysinfo = calloc(1, sizeof(sysinfo_t));
  if (!sysinfo) {
    return ZX_ERR_NO_MEMORY;
  }

  mtx_init(&sysinfo->lock, mtx_plain);

  device_add_args_t args = {
      .version = DEVICE_ADD_ARGS_VERSION,
      .name = "sysinfo",
      .ctx = sysinfo,
      .ops = &sysinfo_ops,
  };

  return device_add(parent, &args, &sysinfo->zxdev);
}

static zx_driver_ops_t sysinfo_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = sysinfo_bind,
};

ZIRCON_DRIVER_BEGIN(sysinfo, sysinfo_driver_ops, "zircon", "0.1", 1)
BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_MISC_PARENT), ZIRCON_DRIVER_END(sysinfo)
