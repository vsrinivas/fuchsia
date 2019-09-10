// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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
#include <zircon/types.h>

#include <memory>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/metadata.h>
#include <ddk/metadata/gpt.h>
#include <ddk/protocol/block.h>
#include <ddk/protocol/block/partition.h>
#include <fbl/auto_call.h>
#include <gpt/c/gpt.h>
#include <gpt/gpt.h>

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

struct gptpart_device_t {
  zx_device_t* zxdev;
  zx_device_t* parent;

  block_impl_protocol_t block_protocol;

  gpt_entry_t gpt_entry;

  block_info_t info;
  size_t block_op_size;
};

class ThreadArgs {
 public:
  static zx_status_t CreateThreadArgs(zx_device_t* parent, gptpart_device_t* first_device,
                                      std::unique_ptr<ThreadArgs>* out);

  ThreadArgs() = delete;
  ThreadArgs(const ThreadArgs&) = delete;
  ThreadArgs(ThreadArgs&&) = delete;
  ThreadArgs& operator=(const ThreadArgs&) = delete;
  ThreadArgs& operator=(ThreadArgs&&) = delete;
  ~ThreadArgs() = default;

  gptpart_device_t* first_device() const { return first_device_; }
  const guid_map_t* guid_map() const { return guid_map_; }
  uint64_t guid_map_entries() const { return guid_map_entries_; }

 private:
  explicit ThreadArgs(gptpart_device_t* first_device) : first_device_(first_device) {}

  gptpart_device_t* first_device_ = {};
  guid_map_t guid_map_[DEVICE_METADATA_GUID_MAP_MAX_ENTRIES] = {};
  uint64_t guid_map_entries_ = {};
};

zx_status_t ThreadArgs::CreateThreadArgs(zx_device_t* parent, gptpart_device_t* first_device,
                                         std::unique_ptr<ThreadArgs>* out) {
  size_t actual;
  auto thread_args = std::unique_ptr<ThreadArgs>(new ThreadArgs(first_device));

  if (thread_args.get() == nullptr) {
    return ZX_ERR_NO_MEMORY;
  }

  zx_status_t status = device_get_metadata(parent, DEVICE_METADATA_GUID_MAP, thread_args->guid_map_,
                                           sizeof(thread_args->guid_map_), &actual);
  // TODO(ZX-4219): We should not continue loading the driver here. Upper layer
  //                may rely on guid to take action on a partition.
  if (status != ZX_OK) {
    zxlogf(INFO, "gpt: device_get_metadata failed (%d)\n", status);
  } else if (actual % sizeof(*thread_args->guid_map_) != 0) {
    zxlogf(INFO, "gpt: GUID map size is invalid (%lu)\n", actual);
  } else {
    thread_args->guid_map_entries_ = actual / sizeof(thread_args->guid_map_[0]);
  }

  *out = std::move(thread_args);
  return ZX_OK;
}

void uint8_to_guid_string(char* dst, uint8_t* src) {
  Guid* guid = (Guid*)src;
  sprintf(dst, "%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X", guid->data1, guid->data2,
          guid->data3, guid->data4[0], guid->data4[1], guid->data4[2], guid->data4[3],
          guid->data4[4], guid->data4[5], guid->data4[6], guid->data4[7]);
}

// TODO(ZX-3241): Ensure the output string of this function is always null-terminated.
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

// implement device protocol:
void gpt_query(void* ctx, block_info_t* bi, size_t* bopsz) {
  gptpart_device_t* gpt = static_cast<gptpart_device_t*>(ctx);
  memcpy(bi, &gpt->info, sizeof(block_info_t));
  *bopsz = gpt->block_op_size;
}

void gpt_queue(void* ctx, block_op_t* bop, block_impl_queue_callback completion_cb, void* cookie) {
  gptpart_device_t* gpt = static_cast<gptpart_device_t*>(ctx);

  switch (bop->command & BLOCK_OP_MASK) {
    case BLOCK_OP_READ:
    case BLOCK_OP_WRITE: {
      size_t blocks = bop->rw.length;
      size_t max = EntryBlockCount(&gpt->gpt_entry).value();

      // Ensure that the request is in-bounds
      if ((bop->rw.offset_dev >= max) || ((max - bop->rw.offset_dev) < blocks)) {
        completion_cb(cookie, ZX_ERR_OUT_OF_RANGE, bop);
        return;
      }

      // Adjust for partition starting block
      bop->rw.offset_dev += gpt->gpt_entry.first;
      break;
    }
    case BLOCK_OP_FLUSH:
      break;
    default:
      completion_cb(cookie, ZX_ERR_NOT_SUPPORTED, bop);
      return;
  }

  block_impl_queue(&gpt->block_protocol, bop, completion_cb, cookie);
}

void gpt_unbind(void* ctx) {
  gptpart_device_t* device = static_cast<gptpart_device_t*>(ctx);
  device_remove(device->zxdev);
}

void gpt_release(void* ctx) {
  gptpart_device_t* device = static_cast<gptpart_device_t*>(ctx);
  free(device);
}

zx_off_t gpt_get_size(void* ctx) {
  gptpart_device_t* dev = static_cast<gptpart_device_t*>(ctx);
  return dev->info.block_count * dev->info.block_size;
}

block_impl_protocol_ops_t block_ops = {
    .query = gpt_query,
    .queue = gpt_queue,
};

static_assert(GPT_GUID_LEN == GUID_LENGTH, "GUID length mismatch");

zx_status_t gpt_get_guid(void* ctx, guidtype_t guidtype, guid_t* out_guid) {
  gptpart_device_t* device = static_cast<gptpart_device_t*>(ctx);
  switch (guidtype) {
    case GUIDTYPE_TYPE:
      memcpy(out_guid, device->gpt_entry.type, GPT_GUID_LEN);
      return ZX_OK;
    case GUIDTYPE_INSTANCE:
      memcpy(out_guid, device->gpt_entry.guid, GPT_GUID_LEN);
      return ZX_OK;
    default:
      return ZX_ERR_INVALID_ARGS;
  }
}

static_assert(GPT_NAME_LEN <= MAX_PARTITION_NAME_LENGTH, "Partition name length mismatch");

zx_status_t gpt_get_name(void* ctx, char* out_name, size_t capacity) {
  if (capacity < GPT_NAME_LEN) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }
  gptpart_device_t* device = static_cast<gptpart_device_t*>(ctx);
  memset(out_name, 0, GPT_NAME_LEN);
  utf16_to_cstring(out_name, device->gpt_entry.name, GPT_NAME_LEN);
  return ZX_OK;
}

block_partition_protocol_ops_t partition_ops = {
    .get_guid = gpt_get_guid,
    .get_name = gpt_get_name,
};

zx_status_t gpt_get_protocol(void* ctx, uint32_t proto_id, void* out) {
  gptpart_device_t* device = static_cast<gptpart_device_t*>(ctx);
  switch (proto_id) {
    case ZX_PROTOCOL_BLOCK_IMPL: {
      auto protocol = static_cast<block_impl_protocol_t*>(out);
      protocol->ctx = device;
      protocol->ops = &block_ops;
      return ZX_OK;
    }
    case ZX_PROTOCOL_BLOCK_PARTITION: {
      auto protocol = static_cast<block_partition_protocol_t*>(out);
      protocol->ctx = device;
      protocol->ops = &partition_ops;
      return ZX_OK;
    }
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }
}

zx_protocol_device_t gpt_proto = []() {
  zx_protocol_device_t gpt = {};
  gpt.version = DEVICE_OPS_VERSION;
  gpt.get_protocol = gpt_get_protocol;
  gpt.unbind = gpt_unbind;
  gpt.release = gpt_release;
  gpt.get_size = gpt_get_size;
  return gpt;
}();

// On failure to add device, frees the device.
zx_status_t DeviceAdd(gptpart_device_t* device, uint32_t partition_number, zx_device_t* parent,
                      uint32_t flags = 0) {
  char name[kDeviceNameLength];
  snprintf(name, sizeof(name), "part-%03u", partition_number);

  device_add_args_t args = {};
  args.version = DEVICE_ADD_ARGS_VERSION;
  args.name = name;
  args.ctx = device;
  args.ops = &gpt_proto;
  args.proto_id = ZX_PROTOCOL_BLOCK_IMPL;
  args.proto_ops = &block_ops;
  args.flags = flags;

  zx_status_t status = device_add(parent, &args, &device->zxdev);

  if (status != ZX_OK) {
    free(device);
  }
  return status;
}

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
    zxlogf(ERROR, "gpt: VMO create failed(%d)\n", status);
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
    zxlogf(ERROR, "gpt: error %d reading GPT\n", bop->command);
    return bop->command;
  }

  return vmo.read(out_buffer, 0, block_info.block_size * block_count);
}

int gpt_bind_thread(void* arg) {
  std::unique_ptr<ThreadArgs> thread_args(static_cast<ThreadArgs*>(arg));
  gptpart_device_t* first_dev = thread_args->first_device();

  auto remove_first_dev = fbl::MakeAutoCall([&first_dev]() { device_remove(first_dev->zxdev); });

  zx_device_t* parent = first_dev->parent;

  const guid_map_t* guid_map = thread_args->guid_map();
  uint64_t guid_map_entries = thread_args->guid_map_entries();

  // used to keep track of number of partitions found
  unsigned partitions = 0;

  block_impl_protocol_t block_protocol;
  memcpy(&block_protocol, &first_dev->block_protocol, sizeof(block_protocol));

  block_info_t block_info;
  size_t block_op_size;
  block_protocol.ops->query(block_protocol.ctx, &block_info, &block_op_size);

  auto result = MinimumBlocksPerCopy(block_info.block_size);
  if (result.is_error()) {
    zxlogf(ERROR, "gpt: block_size(%u) minimum blocks failed: %d\n", block_info.block_size,
           result.error());
    return -1;
  } else if (result.value() > UINT32_MAX) {
    zxlogf(ERROR, "gpt: number of blocks(%lu) required for gpt is too large!\n", result.value());
    return -1;
  }

  auto minimum_device_blocks = MinimumBlockDeviceSize(block_info.block_size);
  if (minimum_device_blocks.is_error()) {
    zxlogf(ERROR, "gpt: failed to get minimum device blocks for block_size(%u)\n",
           block_info.block_size);
    return -1;
  }

  if (block_info.block_count <= minimum_device_blocks.value()) {
    zxlogf(ERROR, "gpt: block device too small to hold GPT required:%lu found:%lu\n",
           minimum_device_blocks.value(), block_info.block_count);
    return -1;
  }

  uint32_t gpt_block_count = static_cast<uint32_t>(result.value());
  size_t gpt_buffer_size = gpt_block_count * block_info.block_size;
  std::unique_ptr<uint8_t[]> buffer(new uint8_t[gpt_buffer_size]);
  fbl::unique_ptr<GptDevice> gpt;

  // sanity check the default txn size with the block size
  if ((kMaxPartitionTableSize % block_info.block_size) ||
      (kMaxPartitionTableSize < block_info.block_size)) {
    zxlogf(ERROR, "gpt: default txn size=%lu is not aligned to blksize=%u!\n",
           kMaxPartitionTableSize, block_info.block_size);
    return -1;
  }

  if (ReadBlocks(&block_protocol, block_op_size, block_info, gpt_block_count, 1, buffer.get()) !=
      ZX_OK) {
    return -1;
  }

  zx_status_t status;
  if ((status = GptDevice::Load(buffer.get(), gpt_buffer_size, block_info.block_size,
                                block_info.block_count, &gpt)) != ZX_OK) {
    zxlogf(ERROR, "gpt: failed to load gpt- %s\n", HeaderStatusToCString(status));
    return -1;
  }

  zxlogf(SPEW, "gpt: found gpt header\n");

  for (partitions = 0; partitions < gpt->EntryCount(); partitions++) {
    auto entry = gpt->GetPartition(partitions);

    if (entry == nullptr) {
      continue;
    }

    auto result = ValidateEntry(entry);
    ZX_DEBUG_ASSERT(result.is_ok());
    ZX_DEBUG_ASSERT(result.value() == true);

    gptpart_device_t* device;
    // use first_dev for first partition
    if (first_dev) {
      device = first_dev;
    } else {
      device = static_cast<gptpart_device_t*>(calloc(1, sizeof(gptpart_device_t)));
      if (!device) {
        zxlogf(ERROR, "gpt: out of memory!\n");
        return -1;
      }
      device->parent = parent;
      memcpy(&device->block_protocol, &block_protocol, sizeof(block_protocol));
    }

    memcpy(&device->gpt_entry, entry, sizeof(gpt_entry_t));
    block_info.block_count = device->gpt_entry.last - device->gpt_entry.first + 1;
    memcpy(&device->info, &block_info, sizeof(block_info));
    device->block_op_size = block_op_size;

    char partition_guid[GPT_GUID_STRLEN];
    uint8_to_guid_string(partition_guid, device->gpt_entry.guid);
    char pname[GPT_NAME_LEN];
    utf16_to_cstring(pname, device->gpt_entry.name, GPT_NAME_LEN);

    apply_guid_map(guid_map, guid_map_entries, pname, device->gpt_entry.type);

    char type_guid[GPT_GUID_STRLEN];
    uint8_to_guid_string(type_guid, device->gpt_entry.type);

    if (first_dev) {
      // make our initial device visible and use if for partition zero
      device_make_visible(first_dev->zxdev);
      first_dev = NULL;
      remove_first_dev.cancel();
    } else {
      zxlogf(SPEW,
             "gpt: partition=%u type=%s guid=%s name=%s first=0x%" PRIx64 " last=0x%" PRIx64 "\n",
             partitions, type_guid, partition_guid, pname, device->gpt_entry.first,
             device->gpt_entry.last);

      DeviceAdd(device, partitions, parent);
    }
  }

  return 0;
}

zx_status_t gpt_bind(void* ctx, zx_device_t* parent) {
  // create an invisible device, which will be used for the first partition
  gptpart_device_t* device = static_cast<gptpart_device_t*>(calloc(1, sizeof(gptpart_device_t)));
  if (!device) {
    return ZX_ERR_NO_MEMORY;
  }
  device->parent = parent;

  std::unique_ptr<ThreadArgs> thread_args;

  if (device_get_protocol(parent, ZX_PROTOCOL_BLOCK, &device->block_protocol) != ZX_OK) {
    zxlogf(ERROR, "gpt: ERROR: block device '%s': does not support block protocol\n",
           device_get_name(parent));
    free(device);
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t status = ThreadArgs::CreateThreadArgs(parent, device, &thread_args);
  if (status != ZX_OK) {
    free(device);
    return status;
  }

  if ((status = DeviceAdd(device, 0, parent, DEVICE_ADD_INVISIBLE)) != ZX_OK) {
    return status;
  }

  // read partition table asynchronously
  thrd_t t;
  status = thrd_create_with_name(&t, gpt_bind_thread, thread_args.get(), "gpt-init");
  if (status != ZX_OK) {
    device_remove(device->zxdev);
  } else {
    thread_args.release();
  }
  return status;
}

constexpr zx_driver_ops_t gpt_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = gpt_bind;
  return ops;
}();

}  // namespace

}  // namespace gpt

// clang-format off
ZIRCON_DRIVER_BEGIN(gpt, gpt::gpt_driver_ops, "zircon", "0.1", 2)
    BI_ABORT_IF_AUTOBIND,
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_BLOCK),
ZIRCON_DRIVER_END(gpt)
    // clang-format on
