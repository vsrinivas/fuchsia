// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_CORRUPT_FS_BLOCK_CLIENT_H_
#define SRC_STORAGE_BLOBFS_CORRUPT_FS_BLOCK_CLIENT_H_

#include <lib/zx/vmo.h>

#include <memory>

#include <block-client/cpp/block-device.h>
#include <fbl/macros.h>

// Wrapper around a BlockDevice that provides a simple ReadBlock/WriteBlock API using blobfs block
// indices instead of device block indices. This class is not threadsafe.
class FsBlockClient {
 public:
  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(FsBlockClient);

  // Creates a new FsBlockClient using the given BlockDevice.
  static zx_status_t Create(std::unique_ptr<block_client::BlockDevice> device,
                            std::unique_ptr<FsBlockClient>* out);

  // Returns the length of this block device in terms of blobfs blocks.
  uint64_t BlockCount() const;

  // Reads the blobfs block into the provided buffer. Data must contain at least kBlobfsBlockSize
  // bytes.
  zx_status_t ReadBlock(uint64_t block, void* data);

  // Writes the blobfs block using the provided buffer. Data must contain at least kBlobfsBlockSize
  // bytes.
  zx_status_t WriteBlock(uint64_t block, const void* data);

 private:
  FsBlockClient(std::unique_ptr<block_client::BlockDevice> device,
                fuchsia_hardware_block_BlockInfo block_info, zx::vmo vmo,
                fuchsia_hardware_block_VmoId vmoid);
  uint64_t device_blocks_per_blobfs_block() const;
  uint64_t fs_block_to_device_block(uint64_t block) const;

  std::unique_ptr<block_client::BlockDevice> device_;
  fuchsia_hardware_block_BlockInfo block_info_;
  zx::vmo vmo_;
  fuchsia_hardware_block_VmoId vmoid_;
};

#endif  // SRC_STORAGE_BLOBFS_CORRUPT_FS_BLOCK_CLIENT_H_
