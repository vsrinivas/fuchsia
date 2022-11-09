// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/security/lib/zxcrypt/ddk-volume.h"

#include <fuchsia/hardware/block/c/banjo.h>
#include <fuchsia/hardware/block/volume/c/banjo.h>
#include <inttypes.h>
#include <lib/sync/completion.h>
#include <lib/zircon-internal/debug.h>
#include <lib/zx/vmo.h>
#include <zircon/assert.h>
#include <zircon/status.h>

#include <memory>
#include <utility>

#include <fbl/alloc_checker.h>

#include "src/security/lib/zxcrypt/volume.h"

#define ZXDEBUG 0

namespace zxcrypt {

void SyncComplete(void* cookie, zx_status_t status, block_op_t* block) {
  // Use the 32bit command field to shuttle the response back to the callsite that's waiting on
  // the completion
  block->command = status;
  sync_completion_signal(static_cast<sync_completion_t*>(cookie));
}

// Performs synchronous I/O
zx_status_t SyncIO(zx_device_t* dev, uint32_t cmd, void* buf, size_t off, size_t len) {
  zx_status_t rc;

  if (!dev || !buf || len == 0) {
    xprintf("bad parameter(s): dev=%p, buf=%p, len=%zu\n", dev, buf, len);
    return ZX_ERR_INVALID_ARGS;
  }

  block_impl_protocol_t proto;
  if ((rc = device_get_protocol(dev, ZX_PROTOCOL_BLOCK, &proto)) != ZX_OK) {
    xprintf("block protocol not support\n");
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx::vmo vmo;
  if ((rc = zx::vmo::create(len, 0, &vmo)) != ZX_OK) {
    xprintf("zx::vmo::create failed: %s\n", zx_status_get_string(rc));
    return rc;
  }

  block_info_t info;
  size_t op_size;
  block_impl_query(&proto, &info, &op_size);

  size_t bsz = info.block_size;
  ZX_DEBUG_ASSERT(off / bsz <= UINT32_MAX);
  ZX_DEBUG_ASSERT(len / bsz <= UINT32_MAX);
  fbl::AllocChecker ac;
  std::unique_ptr<char[]> raw;
  if constexpr (alignof(block_op_t) > __STDCPP_DEFAULT_NEW_ALIGNMENT__) {
    raw = std::unique_ptr<char[]>(
        new (static_cast<std::align_val_t>(alignof(block_op_t)), &ac) char[op_size]);
  } else {
    raw = std::unique_ptr<char[]>(new (&ac) char[op_size]);
  }
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  block_op_t* block = reinterpret_cast<block_op_t*>(raw.get());

  sync_completion_t completion;
  sync_completion_reset(&completion);

  block->command = cmd;
  block->rw.vmo = vmo.get();
  block->rw.length = static_cast<uint32_t>(len / bsz);
  block->rw.offset_dev = static_cast<uint32_t>(off / bsz);
  block->rw.offset_vmo = 0;

  if (cmd == BLOCK_OP_WRITE && (rc = vmo.write(buf, 0, len)) != ZX_OK) {
    xprintf("zx::vmo::write failed: %s\n", zx_status_get_string(rc));
    return rc;
  }

  block_impl_queue(&proto, block, SyncComplete, &completion);
  sync_completion_wait(&completion, ZX_TIME_INFINITE);

  rc = block->command;
  if (rc != ZX_OK) {
    xprintf("Block I/O failed: %s\n", zx_status_get_string(rc));
    return rc;
  }

  if (cmd == BLOCK_OP_READ && (rc = vmo.read(buf, 0, len)) != ZX_OK) {
    xprintf("zx::vmo::read failed: %s\n", zx_status_get_string(rc));
    return rc;
  }

  return ZX_OK;
}

DdkVolume::DdkVolume(zx_device_t* dev) : Volume(), dev_(dev) {}

zx_status_t DdkVolume::Bind(crypto::Cipher::Direction direction, crypto::Cipher* cipher) const {
  zx_status_t rc;
  ZX_DEBUG_ASSERT(dev_);  // Cannot bind from library

  if (!cipher) {
    xprintf("bad parameter(s): cipher=%p\n", cipher);
    return ZX_ERR_INVALID_ARGS;
  }
  if (!block_.get()) {
    xprintf("not initialized\n");
    return ZX_ERR_BAD_STATE;
  }
  if ((rc = cipher->Init(cipher_, direction, data_key_, data_iv_, block_.len())) != ZX_OK) {
    return rc;
  }

  return ZX_OK;
}

zx_status_t DdkVolume::Unlock(zx_device_t* dev, const crypto::Secret& key, key_slot_t slot,
                              std::unique_ptr<DdkVolume>* out) {
  zx_status_t rc;
  std::unique_ptr<DdkVolume> volume;
  if ((rc = DdkVolume::OpenOpaque(dev, &volume)) != ZX_OK) {
    return rc;
  }

  if ((rc = volume->Unlock(key, slot)) != ZX_OK) {
    xprintf("volume->Unlock() failed: %s\n", zx_status_get_string(rc));
    return rc;
  }

  *out = std::move(volume);
  return ZX_OK;
}

zx_status_t DdkVolume::OpenOpaque(zx_device_t* dev, std::unique_ptr<DdkVolume>* out) {
  zx_status_t rc;

  if (!dev || !out) {
    xprintf("bad parameter(s): dev=%p, out=%p\n", dev, out);
    return ZX_ERR_INVALID_ARGS;
  }
  fbl::AllocChecker ac;
  std::unique_ptr<DdkVolume> volume(new (&ac) DdkVolume(dev));
  if (!ac.check()) {
    xprintf("allocation failed: %zu bytes\n", sizeof(DdkVolume));
    return ZX_ERR_NO_MEMORY;
  }
  if ((rc = volume->Init()) != ZX_OK) {
    xprintf("volume->Init() failed: %s\n", zx_status_get_string(rc));
    return rc;
  }

  *out = std::move(volume);
  return ZX_OK;
}

zx_status_t DdkVolume::Init() { return Volume::Init(); }

zx_status_t DdkVolume::Unlock(const crypto::Secret& key, key_slot_t slot) {
  return Volume::Unlock(key, slot);
}

zx_status_t DdkVolume::GetBlockInfo(BlockInfo* out) {
  zx_status_t rc;
  block_impl_protocol_t proto;
  if ((rc = device_get_protocol(dev_, ZX_PROTOCOL_BLOCK, &proto)) != ZX_OK) {
    xprintf("block protocol not supported\n");
    return ZX_ERR_NOT_SUPPORTED;
  }

  block_info_t info;
  size_t block_op_size;
  block_impl_query(&proto, &info, &block_op_size);

  out->block_count = info.block_count;
  out->block_size = info.block_size;
  return ZX_OK;
}

zx_status_t DdkVolume::GetFvmSliceSize(uint64_t* out) {
  zx_status_t rc;
  block_volume_protocol_t proto;
  if ((rc = device_get_protocol(dev_, ZX_PROTOCOL_BLOCK_VOLUME, &proto)) != ZX_OK) {
    xprintf("block volume protocol not supported\n");
    return ZX_ERR_NOT_SUPPORTED;
  }

  volume_manager_info_t manager_info;
  volume_info_t volume_info;
  if ((rc = block_volume_get_info(&proto, &manager_info, &volume_info)) != ZX_OK) {
    return rc;
  }

  *out = manager_info.slice_size;
  return ZX_OK;
}

zx_status_t DdkVolume::DoBlockFvmVsliceQuery(uint64_t vslice_start,
                                             SliceRegion ranges[MAX_SLICE_REGIONS],
                                             uint64_t* slice_count) {
  static_assert(MAX_SLICE_QUERY_REQUESTS == Volume::MAX_SLICE_REGIONS,
                "block volume slice response count must match");
  zx_status_t rc;
  block_volume_protocol_t proto;
  if ((rc = device_get_protocol(dev_, ZX_PROTOCOL_BLOCK_VOLUME, &proto)) != ZX_OK) {
    xprintf("block volume protocol not supported\n");
    return ZX_ERR_NOT_SUPPORTED;
  }

  slice_region_t tmp_ranges[MAX_SLICE_QUERY_REQUESTS];
  size_t range_count;
  if ((rc = block_volume_query_slices(&proto, &vslice_start, 1, tmp_ranges,
                                      MAX_SLICE_QUERY_REQUESTS, &range_count)) != ZX_OK) {
    return rc;
  }
  if (range_count > Volume::MAX_SLICE_REGIONS) {
    // Should be impossible.  Trust nothing.
    return ZX_ERR_BAD_STATE;
  }

  *slice_count = range_count;
  for (size_t i = 0; i < range_count; i++) {
    ranges[i].allocated = tmp_ranges[i].allocated;
    ranges[i].count = tmp_ranges[i].count;
  }
  return ZX_OK;
}

zx_status_t DdkVolume::DoBlockFvmExtend(uint64_t start_slice, uint64_t slice_count) {
  zx_status_t rc;
  block_volume_protocol_t proto;
  if ((rc = device_get_protocol(dev_, ZX_PROTOCOL_BLOCK_VOLUME, &proto)) != ZX_OK) {
    xprintf("block volume protocol not supported\n");
    return ZX_ERR_NOT_SUPPORTED;
  }

  slice_extent_t extent;
  extent.offset = start_slice;
  extent.length = slice_count;
  return block_volume_extend(&proto, &extent);
}

zx_status_t DdkVolume::Read() {
  return SyncIO(dev_, BLOCK_OP_READ, block_.get(), offset_, block_.len());
}

zx_status_t DdkVolume::Write() {
  return SyncIO(dev_, BLOCK_OP_WRITE, block_.get(), offset_, block_.len());
}

zx_status_t DdkVolume::Flush() {
  zx_status_t rc;

  block_impl_protocol_t proto;
  if ((rc = device_get_protocol(dev_, ZX_PROTOCOL_BLOCK, &proto)) != ZX_OK) {
    xprintf("block protocol not support\n");
    return ZX_ERR_NOT_SUPPORTED;
  }

  block_info_t info;
  size_t op_size;
  block_impl_query(&proto, &info, &op_size);

  fbl::AllocChecker ac;
  std::unique_ptr<char[]> raw;
  if constexpr (alignof(block_op_t) > __STDCPP_DEFAULT_NEW_ALIGNMENT__) {
    raw = std::unique_ptr<char[]>(
        new (static_cast<std::align_val_t>(alignof(block_op_t)), &ac) char[op_size]);
  } else {
    raw = std::unique_ptr<char[]>(new (&ac) char[op_size]);
  }
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  block_op_t* block = reinterpret_cast<block_op_t*>(raw.get());

  sync_completion_t completion;
  sync_completion_reset(&completion);

  block->command = BLOCK_OP_FLUSH;

  block_impl_queue(&proto, block, SyncComplete, &completion);
  sync_completion_wait(&completion, ZX_TIME_INFINITE);

  rc = block->command;
  if (rc != ZX_OK) {
    xprintf("Block I/O (BLOCK_OP_FLUSH) failed: %s\n", zx_status_get_string(rc));
    return rc;
  }

  return ZX_OK;
}

}  // namespace zxcrypt
