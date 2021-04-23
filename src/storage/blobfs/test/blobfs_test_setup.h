// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_TEST_BLOBFS_TEST_SETUP_H_
#define SRC_STORAGE_BLOBFS_TEST_BLOBFS_TEST_SETUP_H_

#include <memory>

#include "src/storage/blobfs/blobfs.h"
#include "src/storage/blobfs/common.h"
#include "src/storage/blobfs/vfs_types.h"

namespace blobfs {

constexpr FilesystemOptions DefaultFilesystemOptions() {
  return FilesystemOptions{
      .num_inodes = 512,
  };
}

// A test base class that can will set up a message loop, vfs, and blobfs instance.
class BlobfsTestSetup {
 public:
  async::Loop& loop() { return loop_; }
  async_dispatcher_t* dispatcher() { return loop_.dispatcher(); }

  // These pointers will be null when not mounted.
  VfsType* vfs() { return vfs_.get(); }
  Blobfs* blobfs() { return blobfs_.get(); }

  // Creates a fake block device, formats it with the given options, and mounts it.
  zx_status_t CreateFormatMount(uint64_t block_count, uint32_t block_size,
                                const FilesystemOptions& fs_options = DefaultFilesystemOptions(),
                                const MountOptions& mount_options = MountOptions());

  zx_status_t Mount(std::unique_ptr<BlockDevice> device,
                    const MountOptions& options = MountOptions());
  std::unique_ptr<BlockDevice> Unmount();

  // Unmounts and remounts using the given options.
  //
  // Any Blob references that the test holds will need to be released before this function is
  // called or the BlobCache destructor will assert that there are live blobs.
  zx_status_t Remount(const MountOptions& options = MountOptions());

 private:
  async::Loop loop_{&kAsyncLoopConfigAttachToCurrentThread};

  std::unique_ptr<VfsType> vfs_;
  std::unique_ptr<Blobfs> blobfs_;
};

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_TEST_BLOBFS_TEST_SETUP_H_
