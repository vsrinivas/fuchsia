// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gpt.h"

#include <assert.h>
#include <fcntl.h>
#include <inttypes.h>
#include <lib/cksum.h>
#include <lib/sync/completion.h>
#include <lib/zx/vmo.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <threads.h>
#include <zircon/assert.h>
#include <zircon/device/block.h>
#include <zircon/syscalls.h>

#include <memory>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>

#include "ddk/driver.h"
#include "zircon/errors.h"
#include "zircon/status.h"

namespace gpt {

namespace {

constexpr size_t kDeviceNameLength = 40;

using gpt_t = gpt_header_t;

struct Guid {
  uint32_t data1;
  uint16_t data2;
  uint16_t data3;
  uint8_t data4[8];
};

void uint8_to_guid_string(char* dst, uint8_t* src) {
  Guid* guid = (Guid*)src;
  sprintf(dst, "%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X", guid->data1, guid->data2,
          guid->data3, guid->data4[0], guid->data4[1], guid->data4[2], guid->data4[3],
          guid->data4[4], guid->data4[5], guid->data4[6], guid->data4[7]);
}

// TODO(http://fxb/33048): Ensure the output string of this function is always null-terminated.
void utf16_to_cstring(char* dst, uint8_t* src, size_t charcount) {
  while (charcount > 0) {
    *dst++ = *src;
    src += 2;  // FIXME cheesy
    charcount -= 2;
  }
}

void apply_guid_map(const guid_map_t* guid_map, size_t entries, const char* name, uint8_t* type) {
  for (size_t i = 0; i < entries; i++) {
    if (strncmp(name, guid_map[i].name, GPT_NAME_LEN) == 0) {
      memcpy(type, guid_map[i].guid, GPT_GUID_LEN);
      return;
    }
  }
}

class DummyDevice;
using DummyDeviceType = ddk::Device<DummyDevice>;

class DummyDevice : public DummyDeviceType {
 public:
  DummyDevice(zx_device_t* parent) : DummyDeviceType(parent) {}
  void DdkRelease() { delete this; }
};

}  // namespace

void PartitionDevice::BlockImplQuery(block_info_t* info_out, size_t* block_op_size_out) {
  memcpy(info_out, &info_, sizeof(info_));
  *block_op_size_out = block_op_size_;
}

void PartitionDevice::BlockImplQueue(block_op_t* bop, block_impl_queue_callback completion_cb,
                                     void* cookie) {
  switch (bop->command & BLOCK_OP_MASK) {
    case BLOCK_OP_READ:
    case BLOCK_OP_WRITE: {
      gpt_entry_t* entry = &gpt_entry_;
      size_t blocks = bop->rw.length;
      size_t max = EntryBlockCount(entry).value();

      // Ensure that the request is in-bounds
      if ((bop->rw.offset_dev >= max) || ((max - bop->rw.offset_dev) < blocks)) {
        completion_cb(cookie, ZX_ERR_OUT_OF_RANGE, bop);
        return;
      }

      // Adjust for partition starting block
      bop->rw.offset_dev += entry->first;
      break;
    }
    case BLOCK_OP_TRIM: {
      gpt_entry_t* entry = &gpt_entry_;
      size_t blocks = bop->trim.length;
      size_t max = EntryBlockCount(entry).value();

      if ((bop->trim.offset_dev >= max) || ((max - bop->trim.offset_dev) < blocks)) {
        completion_cb(cookie, ZX_ERR_OUT_OF_RANGE, bop);
        return;
      }

      bop->trim.offset_dev += entry->first;
      break;
    }
    case BLOCK_OP_FLUSH:
      break;
    default:
      completion_cb(cookie, ZX_ERR_NOT_SUPPORTED, bop);
      return;
  }

  block_impl_queue(&block_protocol_, bop, completion_cb, cookie);
}

void PartitionDevice::DdkUnbindNew(ddk::UnbindTxn txn) { txn.Reply(); }

void PartitionDevice::DdkRelease() { delete this; }

zx_off_t PartitionDevice::DdkGetSize() { return info_.block_count * info_.block_size; }

static_assert(GPT_GUID_LEN == GUID_LENGTH, "GUID length mismatch");

zx_status_t PartitionDevice::BlockPartitionGetGuid(guidtype_t guid_type, guid_t* out_guid) {
  switch (guid_type) {
    case GUIDTYPE_TYPE:
      memcpy(out_guid, gpt_entry_.type, GPT_GUID_LEN);
      return ZX_OK;
    case GUIDTYPE_INSTANCE:
      memcpy(out_guid, gpt_entry_.guid, GPT_GUID_LEN);
      return ZX_OK;
    default:
      return ZX_ERR_INVALID_ARGS;
  }
}

static_assert(GPT_NAME_LEN <= MAX_PARTITION_NAME_LENGTH, "Partition name length mismatch");

zx_status_t PartitionDevice::BlockPartitionGetName(char* out_name, size_t capacity) {
  if (capacity < GPT_NAME_LEN) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }
  memset(out_name, 0, GPT_NAME_LEN);
  utf16_to_cstring(out_name, gpt_entry_.name, GPT_NAME_LEN);
  return ZX_OK;
}

zx_status_t PartitionDevice::DdkGetProtocol(uint32_t proto_id, void* out) {
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

void PartitionDevice::SetInfo(gpt_entry_t* entry, block_info_t* info, size_t op_size) {
  memcpy(&gpt_entry_, entry, sizeof(gpt_entry_));
  memcpy(&info_, info, sizeof(info_));
  block_op_size_ = op_size;
}

zx_status_t PartitionDevice::Add(uint32_t partition_number, uint32_t flags) {
  char name[kDeviceNameLength];
  snprintf(name, sizeof(name), "part-%03u", partition_number);

  zx_status_t status = DdkAdd(name);
  if (status != ZX_OK) {
    zxlogf(ERROR, "gpt: DdkAdd failed (%d)", status);
  }
  return status;
}

void PartitionDevice::AsyncRemove() { device_async_remove(zxdev_); }

void gpt_read_sync_complete(void* cookie, zx_status_t status, block_op_t* bop) {
  // Pass 32bit status back to caller via 32bit command field
  // Saves from needing custom structs, etc.
  bop->command = status;
  sync_completion_signal((sync_completion_t*)cookie);
}

zx_status_t ReadBlocks(block_impl_protocol_t* block_protocol, size_t block_op_size,
                       const block_info_t& block_info, uint32_t block_count, uint64_t block_offset,
                       uint8_t* out_buffer) {
  zx_status_t status;
  sync_completion_t completion;
  std::unique_ptr<uint8_t[]> bop_buffer(new uint8_t[block_op_size]);
  block_op_t* bop = reinterpret_cast<block_op_t*>(bop_buffer.get());
  zx::vmo vmo;
  if ((status = zx::vmo::create(block_count * block_info.block_size, 0, &vmo)) != ZX_OK) {
    zxlogf(ERROR, "gpt: VMO create failed(%d)", status);
    return status;
  }

  bop->command = BLOCK_OP_READ;
  bop->rw.vmo = vmo.get();
  bop->rw.length = block_count;
  bop->rw.offset_dev = block_offset;
  bop->rw.offset_vmo = 0;

  block_protocol->ops->queue(block_protocol->ctx, bop, gpt_read_sync_complete, &completion);
  sync_completion_wait(&completion, ZX_TIME_INFINITE);
  if (bop->command != ZX_OK) {
    zxlogf(ERROR, "gpt: error %d reading GPT", bop->command);
    return bop->command;
  }

  return vmo.read(out_buffer, 0, block_info.block_size * block_count);
}

zx_status_t PartitionTable::CreateAndBind(void* ctx, zx_device_t* parent) {
  TableRef tab;
  zx_status_t status = Create(parent, &tab);
  if (status != ZX_OK) {
    return status;
  }
  return tab->Bind();
}

zx_status_t PartitionTable::Create(zx_device_t* parent, TableRef* out,
                                   fbl::Vector<std::unique_ptr<PartitionDevice>>* devices) {
  fbl::AllocChecker ac;
  TableRef tab = fbl::AdoptRef(new (&ac) PartitionTable(parent));
  if (!ac.check()) {
    zxlogf(ERROR, "gpt: out of memory");
    return ZX_ERR_NO_MEMORY;
  }
  tab->devices_ = devices;
  *out = std::move(tab);
  return ZX_OK;
}

zx_status_t PartitionTable::Bind() {
  size_t actual;
  zx_status_t status;
  status =
      device_get_metadata(parent_, DEVICE_METADATA_GUID_MAP, guid_map_, sizeof(guid_map_), &actual);
  // TODO(http://fxb/33999): We should not continue loading the driver here. Upper layer
  //                may rely on guid to take action on a partition.
  if (status != ZX_OK) {
    zxlogf(INFO, "gpt: device_get_metadata failed (%d)", status);
  } else if (actual % sizeof(guid_map_[0]) != 0) {
    zxlogf(INFO, "gpt: GUID map size is invalid (%lu)", actual);
  } else {
    guid_map_entries_ = actual / sizeof(guid_map_[0]);
  }

  block_impl_protocol_t block_protocol;
  if (device_get_protocol(parent_, ZX_PROTOCOL_BLOCK, &block_protocol) != ZX_OK) {
    zxlogf(ERROR, "gpt: ERROR: block device '%s': does not support block protocol",
           device_get_name(parent_));
    return ZX_ERR_NOT_SUPPORTED;
  }

  block_info_t block_info;
  size_t block_op_size;
  block_protocol.ops->query(block_protocol.ctx, &block_info, &block_op_size);

  auto result = MinimumBlocksPerCopy(block_info.block_size);
  if (result.is_error()) {
    zxlogf(ERROR, "gpt: block_size(%u) minimum blocks failed: %d", block_info.block_size,
           result.error());
    return result.error();
  } else if (result.value() > UINT32_MAX) {
    zxlogf(ERROR, "gpt: number of blocks(%lu) required for gpt is too large!", result.value());
    return ZX_ERR_OUT_OF_RANGE;
  }

  auto minimum_device_blocks = MinimumBlockDeviceSize(block_info.block_size);
  if (minimum_device_blocks.is_error()) {
    zxlogf(ERROR, "gpt: failed to get minimum device blocks for block_size(%u)",
           block_info.block_size);
    return minimum_device_blocks.error();
  }

  if (block_info.block_count <= minimum_device_blocks.value()) {
    zxlogf(ERROR, "gpt: block device too small to hold GPT required:%lu found:%lu",
           minimum_device_blocks.value(), block_info.block_count);
    return ZX_ERR_NO_SPACE;
  }

  uint32_t gpt_block_count = static_cast<uint32_t>(result.value());
  size_t gpt_buffer_size = gpt_block_count * block_info.block_size;
  std::unique_ptr<uint8_t[]> buffer(new uint8_t[gpt_buffer_size]);
  std::unique_ptr<GptDevice> gpt;

  // sanity check the default txn size with the block size
  if ((kMaxPartitionTableSize % block_info.block_size) ||
      (kMaxPartitionTableSize < block_info.block_size)) {
    zxlogf(ERROR, "gpt: default txn size=%lu is not aligned to blksize=%u!", kMaxPartitionTableSize,
           block_info.block_size);
    return ZX_ERR_BAD_STATE;
  }

  status = ReadBlocks(&block_protocol, block_op_size, block_info, gpt_block_count, 1, buffer.get());
  if (status != ZX_OK) {
    return status;
  }

  if ((status = GptDevice::Load(buffer.get(), gpt_buffer_size, block_info.block_size,
                                block_info.block_count, &gpt)) != ZX_OK) {
    zxlogf(ERROR, "gpt: failed to load gpt- %s", HeaderStatusToCString(status));
    return status;
  }

  zxlogf(TRACE, "gpt: found gpt header");

  bool has_partition = false;
  unsigned int partitions;
  for (partitions = 0; partitions < gpt->EntryCount(); partitions++) {
    auto entry = gpt->GetPartition(partitions);
    if (entry == nullptr) {
      continue;
    }
    has_partition = true;

    auto result = ValidateEntry(entry);
    ZX_DEBUG_ASSERT(result.is_ok());
    ZX_DEBUG_ASSERT(result.value() == true);

    fbl::AllocChecker ac;
    std::unique_ptr<PartitionDevice> device(new (&ac) PartitionDevice(parent_, &block_protocol));
    if (!ac.check()) {
      zxlogf(ERROR, "gpt: out of memory");
      return ZX_ERR_NO_MEMORY;
    }

    char partition_guid[GPT_GUID_STRLEN];
    uint8_to_guid_string(partition_guid, entry->guid);
    char pname[GPT_NAME_LEN];
    utf16_to_cstring(pname, entry->name, GPT_NAME_LEN);

    apply_guid_map(guid_map_, guid_map_entries_, pname, entry->type);

    char type_guid[GPT_GUID_STRLEN];
    uint8_to_guid_string(type_guid, entry->type);
    zxlogf(TRACE,
           "gpt: partition=%u type=%s guid=%s name=%s first=0x%" PRIx64 " last=0x%" PRIx64 "\n",
           partitions, type_guid, partition_guid, pname, entry->first, entry->last);

    block_info.block_count = entry->last - entry->first + 1;
    device->SetInfo(entry, &block_info, block_op_size);

    if ((status = device->Add(partitions)) != ZX_OK) {
      return status;
    }
    if (devices_ != nullptr) {
      devices_->push_back(std::move(device));
    } else {
      device.release();
    }
  }

  if (!has_partition) {
    auto dummy = std::make_unique<DummyDevice>(parent_);
    status = dummy->DdkAdd("dummy", DEVICE_ADD_NON_BINDABLE);
    if (status != ZX_OK) {
      zxlogf(ERROR, "gpt: failed to add dummy %s", zx_status_get_string(status));
      return status;
    }
    // Dummy is managed by ddk.
    __UNUSED auto p = dummy.release();
    return ZX_OK;
  }

  return ZX_OK;
}

constexpr zx_driver_ops_t gpt_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = PartitionTable::CreateAndBind;
  return ops;
}();

}  // namespace gpt

// clang-format off
ZIRCON_DRIVER_BEGIN(gpt, gpt::gpt_driver_ops, "zircon", "0.1", 2)
    BI_ABORT_IF_AUTOBIND,
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_BLOCK),
ZIRCON_DRIVER_END(gpt)
    // clang-format on
