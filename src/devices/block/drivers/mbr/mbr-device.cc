// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mbr-device.h"

#include <fuchsia/hardware/block/c/banjo.h>
#include <fuchsia/hardware/block/partition/c/banjo.h>
#include <inttypes.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/sync/completion.h>
#include <lib/zx/vmo.h>
#include <string.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/errors.h>
#include <zircon/hw/gpt.h>
#include <zircon/status.h>
#include <zircon/threads.h>
#include <zircon/types.h>

#include <algorithm>
#include <memory>
#include <utility>

#include <fbl/alloc_checker.h>
#include <fbl/string.h>
#include <fbl/vector.h>
#include <gpt/c/gpt.h>

#include "mbr.h"
#include "src/devices/block/drivers/mbr/mbr_bind.h"

namespace {

// ATTN: MBR supports 8 bit partition types instead of GUIDs. Here we define
// mappings between partition type and GUIDs that zircon understands. When
// the MBR driver receives a request for the type GUID, we lie and return the
// a mapping from partition type to type GUID.
const uint8_t kDataGuid[GPT_GUID_LEN] = GUID_DATA_VALUE;
const uint8_t kSysGuid[GPT_GUID_LEN] = GUID_SYSTEM_VALUE;
const uint8_t kMicrosoftDataGuid[GPT_GUID_LEN] = GPT_MICROSOFT_BASIC_DATA_TYPE_GUID;

const uint8_t kSupportedPartitionTypes[] = {
    mbr::kPartitionTypeFuchsiaData, mbr::kPartitionTypeFuchsiaSys, mbr::kPartitionTypeFat12,
    mbr::kPartitionTypeFat16,       mbr::kPartitionTypeFat16B,     mbr::kPartitionTypeFat16LBA,
    mbr::kPartitionTypeFat32,       mbr::kPartitionTypeFat32LBA,
};

constexpr uint32_t DivRoundUp(uint32_t n, uint32_t d) { return (n + (d - 1)) / d; }

zx_status_t MbrReadHeader(const ddk::BlockProtocolClient& parent_proto, mbr::Mbr* mbr_out,
                          block_info_t* block_info_out, size_t* block_op_size_out) {
  parent_proto.Query(block_info_out, block_op_size_out);

  fbl::AllocChecker ac;
  std::unique_ptr<char[]> raw(new (&ac) char[*block_op_size_out]);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  auto* bop = reinterpret_cast<block_op_t*>(raw.get());

  // We need to read at least 512B to parse the MBR. Determine if we should
  // read the device's block size or we should ready exactly 512B.
  uint32_t iosize = 0;
  if (block_info_out->block_size >= mbr::kMbrSize) {
    iosize = block_info_out->block_size;
  } else {
    // Make sure we're reading some multiple of the block size.
    iosize = DivRoundUp(mbr::kMbrSize, block_info_out->block_size) * block_info_out->block_size;
  }

  zx::vmo vmo;
  auto status = zx::vmo::create(iosize, 0, &vmo);
  if (status != ZX_OK) {
    zxlogf(ERROR, "mbr: cannot allocate vmo: %s", zx_status_get_string(status));
    return status;
  }

  sync_completion_t read_complete;

  bop->command = BLOCK_OP_READ;
  bop->rw.vmo = vmo.get();
  bop->rw.length = iosize / block_info_out->block_size;
  bop->rw.offset_dev = 0;
  bop->rw.offset_vmo = 0;

  zxlogf(TRACE, "mbr: Reading header from parent block device");

  parent_proto.Queue(
      bop,
      [](void* cookie, zx_status_t status, block_op_t* bop) {
        bop->command = status;
        sync_completion_signal(static_cast<sync_completion_t*>(cookie));
      },
      &read_complete);
  sync_completion_wait(&read_complete, ZX_TIME_INFINITE);

  if ((status = bop->command) != ZX_OK) {
    zxlogf(ERROR, "mbr: could not read mbr from device: %s", zx_status_get_string(status));
    return status;
  }

  uint8_t buffer[mbr::kMbrSize];
  if ((status = vmo.read(buffer, 0, sizeof(buffer))) != ZX_OK) {
    zxlogf(ERROR, "mbr: Failed to read MBR header: %s", zx_status_get_string(status));
    return status;
  }

  if ((status = mbr::Parse(buffer, sizeof(buffer), mbr_out)) != ZX_OK) {
    zxlogf(ERROR, "mbr: Failed to parse MBR: %s", zx_status_get_string(status));
    return status;
  }
  return ZX_OK;
}

}  // namespace

namespace mbr {

bool MbrDevice::SupportsPartitionType(uint8_t partition_type) {
  return std::end(kSupportedPartitionTypes) != std::find(std::begin(kSupportedPartitionTypes),
                                                         std::end(kSupportedPartitionTypes),
                                                         partition_type);
}

void MbrDevice::BlockImplQuery(block_info_t* info_out, size_t* block_op_size_out) {
  *info_out = info_;
  *block_op_size_out = block_op_size_;
}

void MbrDevice::BlockImplQueue(block_op_t* operation, block_impl_queue_callback completion_cb,
                               void* cookie) {
  switch (operation->command & BLOCK_OP_MASK) {
    case BLOCK_OP_READ:
    case BLOCK_OP_WRITE: {
      size_t blocks = operation->rw.length;
      size_t max = partition_.num_sectors;

      // Ensure that the request is in-bounds
      if ((operation->rw.offset_dev >= max) || ((max - operation->rw.offset_dev) < blocks)) {
        completion_cb(cookie, ZX_ERR_OUT_OF_RANGE, operation);
        return;
      }

      // Adjust for partition starting block
      operation->rw.offset_dev += partition_.start_sector_lba;
      break;
    }
    case BLOCK_OP_FLUSH:
      break;
    default:
      completion_cb(cookie, ZX_ERR_NOT_SUPPORTED, operation);
      return;
  }

  parent_protocol_.Queue(operation, completion_cb, cookie);
}

zx_status_t MbrDevice::BlockPartitionGetGuid(guidtype_t guid_type, guid_t* out_guid) {
  if (guid_type != GUIDTYPE_TYPE) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  switch (partition_.type) {
    case kPartitionTypeFuchsiaData: {
      memcpy(out_guid, kDataGuid, BLOCK_GUID_LEN);
      return ZX_OK;
    }
    case kPartitionTypeFuchsiaSys: {
      memcpy(out_guid, kSysGuid, BLOCK_GUID_LEN);
      return ZX_OK;
    }
    case kPartitionTypeFat12:
    case kPartitionTypeFat16:
    case kPartitionTypeFat16B:
    case kPartitionTypeFat16LBA:
    case kPartitionTypeFat32:
    case kPartitionTypeFat32LBA: {
      memcpy(out_guid, kMicrosoftDataGuid, BLOCK_GUID_LEN);
      return ZX_OK;
    }
    default: {
      zxlogf(ERROR, "mbr: Partition type 0x%02x unsupported", partition_.type);
      return ZX_ERR_NOT_SUPPORTED;
    }
  }
}

zx_status_t MbrDevice::BlockPartitionGetName(char* out_name, size_t capacity) {
  if (capacity < GPT_NAME_LEN) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }
  strlcpy(out_name, name_.c_str(), capacity);
  return ZX_OK;
}

void MbrDevice::DdkRelease() { delete this; }

zx_off_t MbrDevice::DdkGetSize() {
  // TODO: use query() results, *but* fvm returns different query and getsize
  // results, and the latter are dynamic...
  return device_get_size(parent_);
}

zx_status_t MbrDevice::DdkGetProtocol(uint32_t proto_id, void* out) {
  auto* proto = static_cast<ddk::AnyProtocol*>(out);
  switch (proto_id) {
    case ZX_PROTOCOL_BLOCK_IMPL: {
      proto->ops = &block_impl_protocol_ops_;
      proto->ctx = this;
      return ZX_OK;
    }
    case ZX_PROTOCOL_BLOCK_PARTITION: {
      proto->ops = &block_partition_protocol_ops_;
      proto->ctx = this;
      return ZX_OK;
    }
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }
}

zx_status_t MbrDevice::Create(zx_device_t* parent,
                              fbl::Vector<std::unique_ptr<MbrDevice>>* devices_out) {
  if (parent == nullptr || devices_out == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }
  ddk::BlockProtocolClient parent_proto(parent);
  if (!parent_proto.is_valid()) {
    zxlogf(ERROR, "mbr: ERROR: Parent device does not support ZX_PROTOCOL_BLOCK");
    return ZX_ERR_NOT_SUPPORTED;
  }

  Mbr mbr;
  block_info_t block_info;
  size_t block_op_size;
  zx_status_t status;
  if ((status = MbrReadHeader(parent_proto, &mbr, &block_info, &block_op_size)) != ZX_OK) {
    return status;
  }

  // Parse the partitions out of the MBR.
  fbl::AllocChecker ac;
  for (unsigned i = 0; i < countof(mbr.partitions); ++i) {
    const auto& entry = mbr.partitions[i];
    if (entry.type == kPartitionTypeNone) {
      // This partition entry is empty and does not refer to a partition, skip it.
      continue;
    }

    if (entry.type == kPartitionTypeGptProtective && i == 0) {
      // If the first partition on the disk has type '0xee', this MBR is not a real MBR,
      // and we should refuse to bind to it.
      return ZX_ERR_NOT_SUPPORTED;
    }

    zxlogf(INFO, "mbr: found partition, entry = %d, type = 0x%02X, start = %u, length = 0x%X",
           i + 1, entry.type, entry.start_sector_lba, entry.num_sectors);

    if (!MbrDevice::SupportsPartitionType(entry.type)) {
      zxlogf(WARNING, "mbr: Not mounting partition %d, unsupported type 0x%02x", i, entry.type);
      continue;
    }

    char name[16];
    snprintf(name, sizeof(name), "part-%03u", i);

    block_info_t info = block_info;
    info.block_count = entry.num_sectors;

    auto device =
        fbl::make_unique_checked<MbrDevice>(&ac, parent, name, entry, info, block_op_size);
    if (!ac.check()) {
      zxlogf(ERROR, "mbr: Failed to allocate partition device");
      return ZX_ERR_NO_MEMORY;
    }

    devices_out->push_back(std::move(device), &ac);
    if (!ac.check()) {
      zxlogf(ERROR, "mbr: Failed to allocate partition device");
      return ZX_ERR_NO_MEMORY;
    }
  }
  return ZX_OK;
}

zx_status_t MbrDevice::Bind(std::unique_ptr<MbrDevice> device) {
  if (device.get() == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }
  zx_status_t status;
  if ((status = device->DdkAdd(device->Name().c_str())) != ZX_OK) {
    zxlogf(ERROR, "mbr: Failed to add partition device: %s", zx_status_get_string(status));
    return status;
  }

  // devmgr owns the device now that it's bound
  __UNUSED auto* dummy = device.release();

  return ZX_OK;
}

}  // namespace mbr

namespace {

zx_status_t CreateAndBind(void* ctx, zx_device_t* parent) {
  fbl::Vector<std::unique_ptr<mbr::MbrDevice>> devices;
  fbl::AllocChecker ac;
  devices.reserve(mbr::kMbrNumPartitions, &ac);
  if (!ac.check()) {
    zxlogf(ERROR, "mbr: Failed to allocate devices container");
    return ZX_ERR_NO_MEMORY;
  }
  zx_status_t status;
  if ((status = mbr::MbrDevice::Create(parent, &devices)) != ZX_OK) {
    return status;
  }
  for (auto& device : devices) {
    if (device != nullptr) {
      if ((status = mbr::MbrDevice::Bind(std::move(device))) != ZX_OK) {
        return status;
      }
    }
  }
  return ZX_OK;
}

}  // namespace

zx_driver_ops_t MbrDriverOps = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = CreateAndBind;
  return ops;
}();

ZIRCON_DRIVER(mbr, MbrDriverOps, "zircon", "0.1");
