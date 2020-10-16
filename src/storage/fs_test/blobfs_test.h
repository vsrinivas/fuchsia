// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FS_TEST_BLOBFS_TEST_H_
#define SRC_STORAGE_FS_TEST_BLOBFS_TEST_H_

#include "src/storage/blobfs/include/blobfs/format.h"
#include "src/storage/fs_test/fs_test.h"

namespace fs_test {

// Support for blobfs.
class BlobfsFilesystem : public FilesystemImpl<BlobfsFilesystem> {
 public:
  zx::status<std::unique_ptr<FilesystemInstance>> Make(
      const TestFilesystemOptions& options) const override;
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
};

}  // namespace fs_test

#endif  // SRC_STORAGE_FS_TEST_BLOBFS_TEST_H_
