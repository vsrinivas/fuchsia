// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/blobfs/test/blobfs_test_setup.h"

#include <block-client/cpp/fake-device.h>
#include <gtest/gtest.h>

#include "src/storage/blobfs/mkfs.h"

namespace blobfs {

zx_status_t BlobfsTestSetup::CreateFormatMount(uint64_t block_count, uint32_t block_size,
                                               const FilesystemOptions& fs_options,
                                               const MountOptions& mount_options) {
  auto device = std::make_unique<block_client::FakeBlockDevice>(block_count, block_size);
  if (zx_status_t status = FormatFilesystem(device.get(), fs_options); status != ZX_OK)
    return status;
  return Mount(std::move(device), mount_options);
}

zx_status_t BlobfsTestSetup::Mount(std::unique_ptr<BlockDevice> device,
                                   const MountOptions& options) {
  EXPECT_EQ(blobfs_, nullptr);  // Should not already be mounted.

  vfs_ = std::make_unique<VfsType>(loop_.dispatcher());
#if ENABLE_BLOBFS_NEW_PAGER
  if (auto status = vfs_->Init(); status.is_error()) {
    vfs_.reset();
    return status.error_value();
  }
#endif

  auto blobfs_or = Blobfs::Create(loop_.dispatcher(), std::move(device), vfs_.get(), options);
  if (blobfs_or.is_error())
    return blobfs_or.error_value();
  blobfs_ = std::move(blobfs_or.value());

  return ZX_OK;
}

std::unique_ptr<BlockDevice> BlobfsTestSetup::Unmount() {
  auto block_device = Blobfs::Destroy(std::move(blobfs_));
  vfs_.reset();
  return block_device;
}

zx_status_t BlobfsTestSetup::Remount(const MountOptions& options) {
  auto block_device = Unmount();
  return Mount(std::move(block_device), options);
}

}  // namespace blobfs
