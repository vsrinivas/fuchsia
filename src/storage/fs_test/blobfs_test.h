// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FS_TEST_BLOBFS_TEST_H_
#define SRC_STORAGE_FS_TEST_BLOBFS_TEST_H_

#include "src/storage/blobfs/format.h"
#include "src/storage/fs_test/fs_test.h"

namespace fs_test {

class BlobfsInstance;

// Support for blobfs.
class BlobfsFilesystem : public FilesystemImplWithDefaultMake<BlobfsFilesystem, BlobfsInstance> {
 public:
  const Traits& GetTraits() const override {
    static Traits traits{
        .can_unmount = true,
        .timestamp_granularity = zx::nsec(1),
        .supports_hard_links = false,
        .supports_mmap = true,
        .supports_resize = false,
        .max_file_size = blobfs::kBlobfsMaxFileSize,
        .in_memory = false,
        .is_case_sensitive = true,
        .supports_sparse_files = false,
    };
    return traits;
  }

  // Opens an existing blobfs file system.  Currently, this only works with ram nand devices, not
  // ram disks.  The data is provided via the vmo provided within the options.
  zx::status<std::unique_ptr<FilesystemInstance>> Open(
      const TestFilesystemOptions& options) const override;
};

}  // namespace fs_test

#endif  // SRC_STORAGE_FS_TEST_BLOBFS_TEST_H_
