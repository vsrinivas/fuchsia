// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/block/drivers/zxcrypt/device-info.h"

#include <inttypes.h>
#include <lib/zx/vmar.h>
#include <zircon/status.h>

#include <ddk/debug.h>
#include <fbl/auto_call.h>
#include <fbl/macros.h>
#include <zxcrypt/volume.h>

#include "src/devices/block/drivers/zxcrypt/extra.h"

namespace zxcrypt {

DeviceInfo::DeviceInfo(zx_device_t* device, const DdkVolume& volume)
    : block_protocol(device),
      partition_protocol(device),
      volume_protocol(device),
      block_device(device),
      block_size(0),
      op_size(0),
      reserved_blocks(volume.reserved_blocks()),
      reserved_slices(volume.reserved_slices()),
      base(nullptr) {
  block_info_t blk;
  block_protocol.Query(&blk, &op_size);
  block_size = blk.block_size;
  op_size += sizeof(extra_op_t);
}

DeviceInfo::DeviceInfo(DeviceInfo&& other)
    : block_protocol(other.block_device),
      partition_protocol(other.block_device),
      volume_protocol(other.block_device),
      block_device(other.block_device),
      block_size(other.block_size),
      op_size(other.op_size),
      reserved_blocks(other.reserved_blocks),
      reserved_slices(other.reserved_slices),
      vmo(std::move(other.vmo)),
      base(other.base) {
  other.block_protocol.clear();
  other.partition_protocol.clear();
  other.volume_protocol.clear();
  other.block_device = nullptr;
  other.block_size = 0;
  other.op_size = 0;
  other.reserved_blocks = 0;
  other.reserved_slices = 0;
  other.base = nullptr;
}

DeviceInfo::~DeviceInfo() {
  if (base == nullptr) {
    return;
  }
  uintptr_t address = reinterpret_cast<uintptr_t>(base);
  base = nullptr;
  zx_status_t rc = zx::vmar::root_self()->unmap(address, Volume::kBufferSize);
  if (rc != ZX_OK) {
    zxlogf(WARNING, "failed to unmap %" PRIu32 " bytes at %" PRIuPTR ": %s", Volume::kBufferSize,
           address, zx_status_get_string(rc));
  }
}

bool DeviceInfo::IsValid() const { return block_protocol.is_valid(); }

zx_status_t DeviceInfo::Reserve(size_t size) {
  ZX_DEBUG_ASSERT(base == nullptr);
  zx_status_t rc;

  if ((rc = zx::vmo::create(size, 0, &vmo)) != ZX_OK) {
    zxlogf(ERROR, "zx::vmo::create failed: %s", zx_status_get_string(rc));
    return rc;
  }
  auto cleanup = fbl::MakeAutoCall([this]() { vmo.reset(); });

  constexpr uint32_t flags = ZX_VM_PERM_READ | ZX_VM_PERM_WRITE;
  uintptr_t address;
  if ((rc = zx::vmar::root_self()->map(flags, 0, vmo, 0, size, &address)) != ZX_OK) {
    zxlogf(ERROR, "zx::vmar::map failed: %s", zx_status_get_string(rc));
    return rc;
  }
  base = reinterpret_cast<uint8_t*>(address);

  cleanup.cancel();
  return ZX_OK;
}

}  // namespace zxcrypt
