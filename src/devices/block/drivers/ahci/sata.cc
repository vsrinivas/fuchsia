// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sata.h"

#include <byteswap.h>
#include <fcntl.h>
#include <inttypes.h>
#include <lib/sync/completion.h>
#include <lib/zx/vmo.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <zircon/device/block.h>
#include <zircon/types.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <fbl/alloc_checker.h>

#include "controller.h"

#define SATA_FLAG_DMA (1 << 0)
#define SATA_FLAG_LBA48 (1 << 1)

namespace ahci {

struct sata_device_t {
  zx_device_t* zxdev = nullptr;
  Controller* controller = nullptr;

  block_info_t info{};

  uint32_t port = 0;
  uint32_t flags = 0;
  uint32_t max_cmd = 0;  // inclusive
};

// Strings are byte-flipped in pairs.
void string_fix(uint16_t* buf, size_t size) {
  for (size_t i = 0; i < (size / 2); i++) {
    buf[i] = bswap_16(buf[i]);
  }
}

static void sata_device_identify_complete(void* cookie, zx_status_t status, block_op_t* op) {
  sata_txn_t* txn = containerof(op, sata_txn_t, bop);
  txn->status = status;
  sync_completion_signal(static_cast<sync_completion_t*>(cookie));
}

#define QEMU_MODEL_ID "QEMU HARDDISK"
#define QEMU_SG_MAX 1024  // Linux kernel limit

static bool model_id_is_qemu(char* model_id) {
  return !memcmp(model_id, QEMU_MODEL_ID, sizeof(QEMU_MODEL_ID) - 1);
}

static zx_status_t sata_device_identify(sata_device_t* dev, Controller* controller,
                                        const char* name) {
  // Set default devinfo
  sata_devinfo_t di;
  di.block_size = 512;
  di.max_cmd = 1;
  controller->SetDevInfo(dev->port, &di);

  // send IDENTIFY DEVICE
  zx::vmo vmo;
  zx_status_t status = zx::vmo::create(512, 0, &vmo);
  if (status != ZX_OK) {
    zxlogf(DEBUG, "sata: error %d allocating vmo", status);
    return status;
  }

  sync_completion_t completion;
  sata_txn_t txn = {};
  txn.bop.rw.vmo = vmo.get();
  txn.bop.rw.length = 1;
  txn.bop.rw.offset_dev = 0;
  txn.bop.rw.offset_vmo = 0;
  txn.cmd = SATA_CMD_IDENTIFY_DEVICE;
  txn.device = 0;
  txn.completion_cb = sata_device_identify_complete;
  txn.cookie = &completion;

  controller->Queue(dev->port, &txn);
  sync_completion_wait(&completion, ZX_TIME_INFINITE);

  if (txn.status != ZX_OK) {
    zxlogf(ERROR, "%s: error %d in device identify", name, txn.status);
    return txn.status;
  }

  // parse results
  int flags = 0;
  sata_devinfo_response_t devinfo;
  status = vmo.read(&devinfo, 0, sizeof(devinfo));
  if (status != ZX_OK) {
    zxlogf(ERROR, "sata: error %d in vmo_read", status);
    return ZX_ERR_INTERNAL;
  }
  vmo.reset();

  // Strings are 16-bit byte-flipped. Fix in place.
  // Strings are NOT null-terminated.
  string_fix(devinfo.serial.word, sizeof(devinfo.serial.word));
  string_fix(devinfo.firmware_rev.word, sizeof(devinfo.firmware_rev.word));
  string_fix(devinfo.model_id.word, sizeof(devinfo.model_id.word));

  zxlogf(INFO, "%s: dev info", name);
  zxlogf(INFO, "  serial=%.*s", SATA_DEVINFO_SERIAL_LEN, devinfo.serial.string);
  zxlogf(INFO, "  firmware rev=%.*s", SATA_DEVINFO_FW_REV_LEN, devinfo.firmware_rev.string);
  zxlogf(INFO, "  model id=%.*s", SATA_DEVINFO_MODEL_ID_LEN, devinfo.model_id.string);

  bool is_qemu = model_id_is_qemu(devinfo.model_id.string);

  uint16_t major = devinfo.major_version;
  zxlogf(INFO, "  major=0x%x ", major);
  switch (32 - __builtin_clz(major) - 1) {
    case 11:
      zxlogf(INFO, "ACS4");
      break;
    case 10:
      zxlogf(INFO, "ACS3");
      break;
    case 9:
      zxlogf(INFO, "ACS2");
      break;
    case 8:
      zxlogf(INFO, "ATA8-ACS");
      break;
    case 7:
    case 6:
    case 5:
      zxlogf(INFO, "ATA/ATAPI");
      break;
    default:
      zxlogf(INFO, "Obsolete");
      break;
  }

  uint16_t cap = devinfo.capabilities_1;
  if (cap & (1 << 8)) {
    zxlogf(INFO, " DMA");
    flags |= SATA_FLAG_DMA;
  } else {
    zxlogf(INFO, " PIO");
  }
  dev->max_cmd = devinfo.queue_depth;
  zxlogf(INFO, " %u commands", dev->max_cmd + 1);

  uint32_t block_size = 512;  // default
  uint64_t block_count = 0;
  if (cap & (1 << 9)) {
    if ((devinfo.sector_size & 0xd000) == 0x5000) {
      block_size = 2 * devinfo.logical_sector_size;
    }
    if (devinfo.command_set1_1 & (1 << 10)) {
      flags |= SATA_FLAG_LBA48;
      block_count = devinfo.lba_capacity2;
      zxlogf(INFO, "  LBA48");
    } else {
      block_count = devinfo.lba_capacity;
      zxlogf(INFO, "  LBA");
    }
    zxlogf(INFO, " %" PRIu64 " sectors,  sector size=%u", block_count, block_size);
  } else {
    zxlogf(INFO, "  CHS unsupported!");
  }
  dev->flags = flags;

  memset(&dev->info, 0, sizeof(dev->info));
  dev->info.block_size = block_size;
  dev->info.block_count = block_count;

  uint32_t max_sg_size = SATA_MAX_BLOCK_COUNT * block_size;  // SATA cmd limit
  if (is_qemu) {
    max_sg_size = MIN(max_sg_size, QEMU_SG_MAX * block_size);
  }
  dev->info.max_transfer_size = MIN(AHCI_MAX_BYTES, max_sg_size);

  // set devinfo on controller
  di.block_size = block_size, di.max_cmd = dev->max_cmd,

  controller->SetDevInfo(dev->port, &di);

  return ZX_OK;
}

// implement device protocol:

static zx_off_t sata_getsize(void* ctx) {
  sata_device_t* device = static_cast<sata_device_t*>(ctx);
  return device->info.block_count * device->info.block_size;
}

static void sata_release(void* ctx) {
  sata_device_t* device = static_cast<sata_device_t*>(ctx);
  delete device;
}

static zx_protocol_device_t sata_device_proto = []() {
  zx_protocol_device_t device = {};
  device.version = DEVICE_OPS_VERSION;
  device.get_size = sata_getsize;
  device.release = sata_release;
  return device;
}();

static void sata_query(void* ctx, block_info_t* info_out, size_t* block_op_size_out) {
  sata_device_t* dev = static_cast<sata_device_t*>(ctx);
  memcpy(info_out, &dev->info, sizeof(*info_out));
  *block_op_size_out = sizeof(sata_txn_t);
}

static void sata_queue(void* ctx, block_op_t* bop, block_impl_queue_callback completion_cb,
                       void* cookie) {
  sata_device_t* dev = static_cast<sata_device_t*>(ctx);
  sata_txn_t* txn = containerof(bop, sata_txn_t, bop);
  txn->completion_cb = completion_cb;
  txn->cookie = cookie;

  switch (BLOCK_OP(bop->command)) {
    case BLOCK_OP_READ:
    case BLOCK_OP_WRITE:
      // complete empty transactions immediately
      if (bop->rw.length == 0) {
        block_complete(txn, ZX_ERR_INVALID_ARGS);
        return;
      }
      // transaction must fit within device
      if ((bop->rw.offset_dev >= dev->info.block_count) ||
          ((dev->info.block_count - bop->rw.offset_dev) < bop->rw.length)) {
        block_complete(txn, ZX_ERR_OUT_OF_RANGE);
        return;
      }

      txn->cmd = (BLOCK_OP(bop->command) == BLOCK_OP_READ) ? SATA_CMD_READ_DMA_EXT
                                                           : SATA_CMD_WRITE_DMA_EXT;
      txn->device = 0x40;
      zxlogf(DEBUG, "sata: queue op 0x%x txn %p", bop->command, txn);
      break;
    case BLOCK_OP_FLUSH:
      zxlogf(DEBUG, "sata: queue FLUSH txn %p", txn);
      break;
    default:
      block_complete(txn, ZX_ERR_NOT_SUPPORTED);
      return;
  }

  dev->controller->Queue(dev->port, txn);
}

static block_impl_protocol_ops_t sata_block_proto = {
    .query = sata_query,
    .queue = sata_queue,
};

zx_status_t sata_bind(Controller* controller, zx_device_t* parent, uint32_t port) {
  // initialize the device
  fbl::AllocChecker ac;
  std::unique_ptr<sata_device_t> device(new (&ac) sata_device_t);
  if (!ac.check()) {
    zxlogf(ERROR, "sata: out of memory");
    return ZX_ERR_NO_MEMORY;
  }
  device->controller = controller;
  device->port = port;

  char name[8];
  snprintf(name, sizeof(name), "sata%u", port);

  // send device identify
  zx_status_t status = sata_device_identify(device.get(), controller, name);
  if (status < 0) {
    return status;
  }

  // add the device
  device_add_args_t args = {};
  args.version = DEVICE_ADD_ARGS_VERSION;
  args.name = name;
  args.ctx = device.get();
  args.ops = &sata_device_proto;
  args.proto_id = ZX_PROTOCOL_BLOCK_IMPL;
  args.proto_ops = &sata_block_proto;

  status = device_add(parent, &args, &device->zxdev);
  if (status < 0) {
    return status;
  }
  device.release();  // Device has been retained by device_add().
  return ZX_OK;
}

}  // namespace ahci
