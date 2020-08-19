// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "utils.h"

#include <memory>

#include <fbl/auto_call.h>
#include <safemath/checked_math.h>
#include <storage/buffer/owned_vmoid.h>
#include <zxtest/zxtest.h>

namespace factoryfs {

namespace {

storage::OwnedVmoid AttachVmo(BlockDevice* device, zx::vmo* vmo) {
  storage::Vmoid vmoid;
  EXPECT_OK(device->BlockAttachVmo(*vmo, &vmoid));
  return storage::OwnedVmoid(std::move(vmoid), device);
}

// Verify that the |size| and |offset| are |device| block size aligned.
// Returns block_size in |out_block_size|.
void VerifySizeBlockAligned(BlockDevice* device, size_t size, uint64_t offset,
                            uint32_t* out_block_size) {
  fuchsia_hardware_block_BlockInfo info = {};
  ASSERT_OK(device->BlockGetInfo(&info));
  ASSERT_EQ(size % info.block_size, 0);
  ASSERT_EQ(offset % info.block_size, 0);
  *out_block_size = info.block_size;
}

}  // namespace

void DeviceBlockRead(BlockDevice* device, void* buf, size_t size, uint64_t dev_offset) {
  uint32_t block_size;
  VerifySizeBlockAligned(device, size, dev_offset, &block_size);

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(size, 0, &vmo));

  storage::OwnedVmoid vmoid = AttachVmo(device, &vmo);

  block_fifo_request_t request;
  request.opcode = BLOCKIO_READ;
  request.vmoid = vmoid.get();
  request.length = safemath::checked_cast<uint32_t>(size / block_size);
  request.vmo_offset = 0;
  request.dev_offset = safemath::checked_cast<uint32_t>(dev_offset / block_size);
  ASSERT_OK(device->FifoTransaction(&request, 1));
  ASSERT_OK(vmo.read(buf, 0, size));
}

void DeviceBlockWrite(BlockDevice* device, const void* buf, size_t size, uint64_t dev_offset) {
  uint32_t block_size;
  VerifySizeBlockAligned(device, size, dev_offset, &block_size);

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(size, 0, &vmo));
  ASSERT_OK(vmo.write(buf, 0, size));

  storage::OwnedVmoid vmoid = AttachVmo(device, &vmo);

  block_fifo_request_t request;
  request.opcode = BLOCKIO_WRITE;
  request.vmoid = vmoid.get();
  request.length = safemath::checked_cast<uint32_t>(size / block_size);
  request.vmo_offset = 0;
  request.dev_offset = safemath::checked_cast<uint32_t>(dev_offset / block_size);
  ASSERT_OK(device->FifoTransaction(&request, 1));
}

}  // namespace factoryfs
