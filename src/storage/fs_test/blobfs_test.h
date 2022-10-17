// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FS_TEST_BLOBFS_TEST_H_
#define SRC_STORAGE_FS_TEST_BLOBFS_TEST_H_

#include "src/storage/blobfs/format.h"
#include "src/storage/fs_test/fs_test.h"

namespace fs_test {

// Support for blobfs.
class BlobfsFilesystem : public FilesystemImplWithDefaultMake<BlobfsFilesystem> {
 public:
  const Traits& GetTraits() const override {
    static Traits traits{
        .max_file_size = blobfs::kBlobfsMaxFileSize,
        .supports_hard_links = false,
        .supports_inspect = true,
        .supports_mmap = true,
        .supports_mmap_shared_write = false,
        .supports_sparse_files = false,
        .timestamp_granularity = zx::nsec(1),
    };
    return traits;
  }

  std::unique_ptr<FilesystemInstance> Create(RamDevice device,
                                             std::string device_path) const override;

  // Opens an existing blobfs file system.  Currently, this only works with ram nand devices, not
  // ram disks.  The data is provided via the vmo provided within the options.
  zx::result<std::unique_ptr<FilesystemInstance>> Open(
      const TestFilesystemOptions& options) const override;
};

}  // namespace fs_test

#endif  // SRC_STORAGE_FS_TEST_BLOBFS_TEST_H_
