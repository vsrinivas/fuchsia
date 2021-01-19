// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <fuchsia/hardware/block/c/banjo.h>
#include <fuchsia/hardware/block/partition/c/banjo.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/boot/image.h>
#include <zircon/types.h>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/metadata.h>

#include "src/devices/block/drivers/bootpart/bootpart_bind.h"

#define GUID_STRLEN 40

#define TXN_SIZE 0x4000  // 128 partition entries

typedef struct {
  zx_device_t* zxdev;
  zx_device_t* parent;

  block_impl_protocol_t bp;
  zbi_partition_t part;

  block_info_t info;
  size_t block_op_size;
} bootpart_device_t;

struct structured_guid {
  uint32_t data1;
  uint16_t data2;
  uint16_t data3;
  uint8_t data4[8];
};

static void uint8_to_guid_string(char* dst, uint8_t* src) {
  struct structured_guid* guid = (struct structured_guid*)src;
  sprintf(dst, "%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X", guid->data1, guid->data2,
          guid->data3, guid->data4[0], guid->data4[1], guid->data4[2], guid->data4[3],
          guid->data4[4], guid->data4[5], guid->data4[6], guid->data4[7]);
}

static uint64_t get_lba_count(bootpart_device_t* dev) {
  // last LBA is inclusive
  return dev->part.last_block - dev->part.first_block + 1;
}

// implement device protocol:

static void bootpart_query(void* ctx, block_info_t* bi, size_t* bopsz) {
  bootpart_device_t* bootpart = ctx;
  memcpy(bi, &bootpart->info, sizeof(block_info_t));
  *bopsz = bootpart->block_op_size;
}

static void bootpart_queue(void* ctx, block_op_t* bop, block_impl_queue_callback completion_cb,
                           void* cookie) {
  bootpart_device_t* bootpart = ctx;

  switch (bop->command & BLOCK_OP_MASK) {
    case BLOCK_OP_READ:
    case BLOCK_OP_WRITE: {
      size_t blocks = bop->rw.length;
      size_t max = get_lba_count(bootpart);

      // Ensure that the request is in-bounds
      if ((bop->rw.offset_dev >= max) || ((max - bop->rw.offset_dev) < blocks)) {
        completion_cb(cookie, ZX_ERR_OUT_OF_RANGE, bop);
        return;
      }

      // Adjust for partition starting block
      bop->rw.offset_dev += bootpart->part.first_block;
      break;
    }
    case BLOCK_OP_FLUSH:
      break;
    default:
      completion_cb(cookie, ZX_ERR_NOT_SUPPORTED, bop);
      return;
  }

  bootpart->bp.ops->queue(bootpart->bp.ctx, bop, completion_cb, cookie);
}

static void bootpart_init(void* ctx) {
  bootpart_device_t* device = ctx;

  // Add empty partition map metadata to prevent this driver from binding to its child devices.
  zx_status_t status = device_add_metadata(device->zxdev, DEVICE_METADATA_PARTITION_MAP, NULL, 0);
  // Make the device visible after adding metadata. If there was an error, this will schedule
  // unbinding of the device.
  return device_init_reply(device->zxdev, status, NULL);
}

static void bootpart_unbind(void* ctx) {
  bootpart_device_t* device = ctx;
  device_unbind_reply(device->zxdev);
}

static void bootpart_release(void* ctx) {
  bootpart_device_t* device = ctx;
  free(device);
}

static zx_off_t bootpart_get_size(void* ctx) {
  bootpart_device_t* dev = ctx;
  // TODO: use query() results, *but* fvm returns different query and getsize
  // results, and the latter are dynamic...
  return device_get_size(dev->parent);
}

static block_impl_protocol_ops_t block_ops = {
    .query = bootpart_query,
    .queue = bootpart_queue,
};

static_assert(ZBI_PARTITION_GUID_LEN == GUID_LENGTH, "GUID length mismatch");

static zx_status_t bootpart_get_guid(void* ctx, guidtype_t guid_type, guid_t* out_guid) {
  bootpart_device_t* device = ctx;
  switch (guid_type) {
    case GUIDTYPE_TYPE:
      memcpy(out_guid, device->part.type_guid, ZBI_PARTITION_GUID_LEN);
      return ZX_OK;
    case GUIDTYPE_INSTANCE:
      memcpy(out_guid, device->part.uniq_guid, ZBI_PARTITION_GUID_LEN);
      return ZX_OK;
    default:
      return ZX_ERR_INVALID_ARGS;
  }
}

static_assert(ZBI_PARTITION_NAME_LEN <= MAX_PARTITION_NAME_LENGTH, "Name length mismatch");

static zx_status_t bootpart_get_name(void* ctx, char* out_name, size_t capacity) {
  if (capacity < ZBI_PARTITION_NAME_LEN) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  bootpart_device_t* device = ctx;
  strlcpy(out_name, device->part.name, ZBI_PARTITION_NAME_LEN);
  return ZX_OK;
}

static block_partition_protocol_ops_t partition_ops = {
    .get_guid = bootpart_get_guid,
    .get_name = bootpart_get_name,
};

static zx_status_t bootpart_get_protocol(void* ctx, uint32_t proto_id, void* out) {
  bootpart_device_t* device = ctx;
  switch (proto_id) {
    case ZX_PROTOCOL_BLOCK_IMPL: {
      block_impl_protocol_t* protocol = out;
      protocol->ctx = device;
      protocol->ops = &block_ops;
      return ZX_OK;
    }
    case ZX_PROTOCOL_BLOCK_PARTITION: {
      block_partition_protocol_t* protocol = out;
      protocol->ctx = device;
      protocol->ops = &partition_ops;
      return ZX_OK;
    }
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }
}

static zx_protocol_device_t device_proto = {
    .version = DEVICE_OPS_VERSION,
    .get_protocol = bootpart_get_protocol,
    .get_size = bootpart_get_size,
    .init = bootpart_init,
    .unbind = bootpart_unbind,
    .release = bootpart_release,
};

static zx_status_t bootpart_bind(void* ctx, zx_device_t* parent) {
  block_impl_protocol_t bp;
  uint8_t buffer[METADATA_PARTITION_MAP_MAX];
  size_t actual;

  if (device_get_protocol(parent, ZX_PROTOCOL_BLOCK, &bp) != ZX_OK) {
    zxlogf(ERROR, "bootpart: block device '%s': does not support block protocol",
           device_get_name(parent));
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t status =
      device_get_metadata(parent, DEVICE_METADATA_PARTITION_MAP, buffer, sizeof(buffer), &actual);
  if (status != ZX_OK) {
    return status;
  }

  zbi_partition_map_t* pmap = (zbi_partition_map_t*)buffer;
  if (pmap->partition_count == 0) {
    zxlogf(ERROR, "bootpart: partition_count is zero");
    return ZX_ERR_INTERNAL;
  }

  block_info_t block_info;
  size_t block_op_size;
  bp.ops->query(bp.ctx, &block_info, &block_op_size);

  for (unsigned i = 0; i < pmap->partition_count; i++) {
    zbi_partition_t* part = &pmap->partitions[i];
    char name[128];
    char type_guid[GUID_STRLEN];
    char uniq_guid[GUID_STRLEN];

    snprintf(name, sizeof(name), "part-%03u", i);
    uint8_to_guid_string(type_guid, part->type_guid);
    uint8_to_guid_string(uniq_guid, part->uniq_guid);

    zxlogf(TRACE,
           "bootpart: partition %u (%s) type=%s guid=%s name=%s first=0x%" PRIx64 " last=0x%" PRIx64
           "\n",
           i, name, type_guid, uniq_guid, part->name, part->first_block, part->last_block);

    bootpart_device_t* device = calloc(1, sizeof(bootpart_device_t));
    if (!device) {
      return ZX_ERR_NO_MEMORY;
    }

    device->parent = parent;
    memcpy(&device->bp, &bp, sizeof(device->bp));
    memcpy(&device->part, part, sizeof(device->part));
    block_info.block_count = device->part.last_block - device->part.first_block + 1;
    memcpy(&device->info, &block_info, sizeof(block_info));
    device->block_op_size = block_op_size;

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = name,
        .ctx = device,
        .ops = &device_proto,
        .proto_id = ZX_PROTOCOL_BLOCK_IMPL,
        .proto_ops = &block_ops,
    };

    zx_status_t status = device_add(parent, &args, &device->zxdev);
    if (status != ZX_OK) {
      free(device);
      return status;
    }
  }
  return ZX_OK;
}

static zx_driver_ops_t bootpart_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = bootpart_bind,
};

ZIRCON_DRIVER(bootpart, bootpart_driver_ops, "zircon", "0.1");
