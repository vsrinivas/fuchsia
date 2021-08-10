// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "src/devices/nand/drivers/nandpart/nandpart.h"

#include <assert.h>
#include <fuchsia/hardware/badblock/c/banjo.h>
#include <inttypes.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/ddk/metadata.h>
#include <lib/operation/nand.h>
#include <lib/sync/completion.h>
#include <stdio.h>
#include <string.h>
#include <zircon/boot/image.h>
#include <zircon/hw/gpt.h>
#include <zircon/types.h>

#include <algorithm>
#include <memory>

#include <ddk/metadata/nand.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>

#include "src/devices/nand/drivers/nandpart/nandpart-bind.h"
#include "src/devices/nand/drivers/nandpart/nandpart-utils.h"

namespace nand {
namespace {

constexpr uint8_t fvm_guid[] = GUID_FVM_VALUE;
constexpr uint8_t test_guid[] = GUID_TEST_VALUE;

struct PrivateStorage {
  uint32_t offset;
};

using NandPartOp = nand::BorrowedOperation<PrivateStorage>;

// Shim for calling sub-partition's callback.
void CompletionCallback(void* cookie, zx_status_t status, nand_operation_t* nand_op) {
  NandPartOp op(nand_op, *static_cast<size_t*>(cookie));
  // Re-translate the offsets.
  switch (op.operation()->command) {
    case NAND_OP_READ_BYTES:
    case NAND_OP_WRITE_BYTES:
      op.operation()->rw_bytes.offset_nand -= op.private_storage()->offset;
      break;
    case NAND_OP_READ:
    case NAND_OP_WRITE:
      op.operation()->rw.offset_nand -= op.private_storage()->offset;
      break;
    case NAND_OP_ERASE:
      op.operation()->erase.first_block -= op.private_storage()->offset;
      break;
    default:
      ZX_ASSERT(false);
  }
  op.Complete(status);
}

}  // namespace

zx_status_t NandPartDevice::Create(void* ctx, zx_device_t* parent) {
  zxlogf(INFO, "NandPartDevice::Create: Starting...!");

  nand_protocol_t nand_proto;
  if (device_get_protocol(parent, ZX_PROTOCOL_NAND, &nand_proto) != ZX_OK) {
    zxlogf(ERROR, "nandpart: parent device '%s': does not support nand protocol",
           device_get_name(parent));
    return ZX_ERR_NOT_SUPPORTED;
  }

  // Query parent to get its nand_info_t and size for nand_operation_t.
  nand_info_t nand_info;
  size_t parent_op_size;
  nand_proto.ops->query(nand_proto.ctx, &nand_info, &parent_op_size);
  // Make sure parent_op_size is aligned, so we can safely add our data at the end.
  parent_op_size = fbl::round_up(parent_op_size, 8u);

  // Query parent for nand configuration info.
  size_t actual;
  nand_config_t nand_config;
  zx_status_t status = device_get_metadata(parent, DEVICE_METADATA_PRIVATE, &nand_config,
                                           sizeof(nand_config), &actual);
  if (status != ZX_OK) {
    zxlogf(ERROR, "nandpart: parent device '%s' has no device metadata", device_get_name(parent));
    return status;
  }
  if (actual < sizeof(nand_config_t)) {
    zxlogf(ERROR, "nandpart: Expected metadata is of size %zu, needs to at least be %zu", actual,
           sizeof(nand_config_t));
    return ZX_ERR_INTERNAL;
  }
  // Create a bad block instance.
  BadBlock::Config config = {
      .bad_block_config = nand_config.bad_block_config,
      .nand_proto = nand_proto,
  };
  fbl::RefPtr<BadBlock> bad_block;
  status = BadBlock::Create(config, &bad_block);
  if (status != ZX_OK) {
    zxlogf(ERROR, "nandpart: Failed to create BadBlock object");
    return status;
  }

  // Query parent for partition map.
  uint8_t buffer[METADATA_PARTITION_MAP_MAX];
  status =
      device_get_metadata(parent, DEVICE_METADATA_PARTITION_MAP, buffer, sizeof(buffer), &actual);
  if (status != ZX_OK) {
    zxlogf(ERROR, "nandpart: parent device '%s' has no partition map", device_get_name(parent));
    return status;
  }
  if (actual < sizeof(zbi_partition_map_t)) {
    zxlogf(ERROR, "nandpart: Partition map is of size %zu, needs to at least be %zu", actual,
           sizeof(zbi_partition_t));
    return ZX_ERR_INTERNAL;
  }

  auto* pmap = reinterpret_cast<zbi_partition_map_t*>(buffer);

  const size_t minimum_size =
      sizeof(zbi_partition_map_t) + (sizeof(zbi_partition_t) * pmap->partition_count);
  if (actual < minimum_size) {
    zxlogf(ERROR, "nandpart: Partition map is of size %zu, needs to at least be %zu", actual,
           minimum_size);
    return ZX_ERR_INTERNAL;
  }

  // Sanity check partition map and transform into expected form.
  status = SanitizePartitionMap(pmap, nand_info);
  if (status != ZX_OK) {
    return status;
  }

  // Create a device for each partition.
  for (unsigned i = 0; i < pmap->partition_count; i++) {
    const auto* part = &pmap->partitions[i];

    nand_info.num_blocks = static_cast<uint32_t>(part->last_block - part->first_block + 1);
    memcpy(&nand_info.partition_guid, &part->type_guid, sizeof(nand_info.partition_guid));
    // We only use FTL for the FVM partition.
    if (memcmp(part->type_guid, fvm_guid, sizeof(fvm_guid)) == 0) {
      nand_info.nand_class = NAND_CLASS_FTL;
    } else if (memcmp(part->type_guid, test_guid, sizeof(test_guid)) == 0) {
      nand_info.nand_class = NAND_CLASS_TEST;
    } else {
      nand_info.nand_class = NAND_CLASS_BBS;
    }

    fbl::AllocChecker ac;
    std::unique_ptr<NandPartDevice> device(
        new (&ac) NandPartDevice(parent, nand_proto, bad_block, parent_op_size, nand_info,
                                 static_cast<uint32_t>(part->first_block)));
    if (!ac.check()) {
      continue;
    }
    // Find optional partition_config information.
    uint32_t copy_count = 1;
    for (uint32_t i = 0; i < nand_config.extra_partition_config_count; i++) {
      if (memcmp(nand_config.extra_partition_config[i].type_guid, part->type_guid,
                 sizeof(part->type_guid)) == 0 &&
          nand_config.extra_partition_config[i].copy_count > 0) {
        copy_count = nand_config.extra_partition_config[i].copy_count;
        break;
      }
    }
    status = device->Bind(part->name, copy_count);
    if (status != ZX_OK) {
      zxlogf(ERROR, "Failed to bind %s with error %d", part->name, status);

      continue;
    }
    // devmgr is now in charge of the device.
    __UNUSED auto* dummy = device.release();
  }

  return ZX_OK;
}

void NandPartDevice::DdkInit(ddk::InitTxn init_txn) {
  // Add empty partition map metadata to prevent this driver from binding to its child devices
  zx_status_t status = DdkAddMetadata(DEVICE_METADATA_PARTITION_MAP, nullptr, 0);
  if (status != ZX_OK) {
    init_txn.Reply(status);
    return;
  }

  status = DdkAddMetadata(DEVICE_METADATA_PRIVATE, &extra_partition_copy_count_,
                          sizeof(extra_partition_copy_count_));
  init_txn.Reply(status);
}

zx_status_t NandPartDevice::Bind(const char* name, uint32_t copy_count) {
  zxlogf(INFO, "nandpart: Binding %s to %s", name, device_get_name(parent()));
  extra_partition_copy_count_ = copy_count;
  zx_device_prop_t props[] = {
      {BIND_PROTOCOL, 0, ZX_PROTOCOL_NAND},
      {BIND_NAND_CLASS, 0, nand_info_.nand_class},
  };

  return DdkAdd(ddk::DeviceAddArgs(name).set_props(props));
}

void NandPartDevice::NandQuery(nand_info_t* info_out, size_t* nand_op_size_out) {
  memcpy(info_out, &nand_info_, sizeof(*info_out));
  // Add size of extra context.
  *nand_op_size_out = NandPartOp::OperationSize(parent_op_size_);
}

void NandPartDevice::NandQueue(nand_operation_t* nand_op, nand_queue_callback completion_cb,
                               void* cookie) {
  NandPartOp op(nand_op, completion_cb, cookie, parent_op_size_);
  uint32_t command = op.operation()->command;

  // Make offset relative to full underlying device
  switch (command) {
    case NAND_OP_READ_BYTES:
    case NAND_OP_WRITE_BYTES:
      op.private_storage()->offset =
          erase_block_start_ * nand_info_.pages_per_block * nand_info_.page_size;
      op.operation()->rw_bytes.offset_nand += op.private_storage()->offset;
      break;
    case NAND_OP_READ:
    case NAND_OP_WRITE:
      op.private_storage()->offset = erase_block_start_ * nand_info_.pages_per_block;
      op.operation()->rw.offset_nand += op.private_storage()->offset;
      break;
    case NAND_OP_ERASE:
      op.private_storage()->offset = erase_block_start_;
      op.operation()->erase.first_block += erase_block_start_;
      break;
    default:
      op.Complete(ZX_ERR_NOT_SUPPORTED);
      return;
  }

  // Call parent's queue
  nand_.Queue(op.take(), CompletionCallback, &parent_op_size_);
}

zx_status_t NandPartDevice::NandGetFactoryBadBlockList(uint32_t* bad_blocks, size_t bad_block_len,
                                                       size_t* num_bad_blocks) {
  // TODO implement this.
  *num_bad_blocks = 0;
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t NandPartDevice::BadBlockGetBadBlockList(uint32_t* bad_block_list,
                                                    size_t bad_block_list_len,
                                                    size_t* bad_block_count) {
  if (!bad_block_list_) {
    const zx_status_t status = bad_block_->GetBadBlockList(
        erase_block_start_, erase_block_start_ + nand_info_.num_blocks - 1, &bad_block_list_);
    if (status != ZX_OK) {
      return status;
    }
    for (uint32_t i = 0; i < bad_block_list_.size(); i++) {
      bad_block_list_[i] -= erase_block_start_;
    }
  }

  *bad_block_count = bad_block_list_.size();
  zxlogf(DEBUG, "nandpart: %s: Bad block count: %zu", name(), *bad_block_count);

  if (bad_block_list_len == 0 || bad_block_list_.size() == 0) {
    return ZX_OK;
  }
  if (bad_block_list == NULL) {
    return ZX_ERR_INVALID_ARGS;
  }

  const size_t size = sizeof(uint32_t) * std::min(*bad_block_count, bad_block_list_len);
  memcpy(bad_block_list, bad_block_list_.data(), size);
  return ZX_OK;
}

zx_status_t NandPartDevice::BadBlockMarkBlockBad(uint32_t block) {
  if (block >= nand_info_.num_blocks) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  // First, invalidate our cached copy.
  bad_block_list_.reset();

  // Second, "write-through" to actually persist.
  block += erase_block_start_;
  return bad_block_->MarkBlockBad(block);
}

zx_status_t NandPartDevice::DdkGetProtocol(uint32_t proto_id, void* protocol) {
  auto* proto = static_cast<ddk::AnyProtocol*>(protocol);
  proto->ctx = this;
  switch (proto_id) {
    case ZX_PROTOCOL_NAND:
      proto->ops = &nand_protocol_ops_;
      break;
    case ZX_PROTOCOL_BAD_BLOCK:
      proto->ops = &bad_block_protocol_ops_;
      break;
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }
  return ZX_OK;
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = NandPartDevice::Create;
  return ops;
}();

}  // namespace nand

ZIRCON_DRIVER(nandpart, nand::driver_ops, "zircon", "0.1");
