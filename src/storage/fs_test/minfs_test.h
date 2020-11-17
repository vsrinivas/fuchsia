// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FS_TEST_MINFS_TEST_H_
#define SRC_STORAGE_FS_TEST_MINFS_TEST_H_

#include "src/storage/fs_test/fs_test.h"
#include "src/storage/minfs/format.h"

namespace fs_test {

// Support for Minfs.
class MinfsFilesystem : public FilesystemImpl<MinfsFilesystem> {
 public:
  zx::status<std::unique_ptr<FilesystemInstance>> Make(
      const TestFilesystemOptions& options) const override;
  zx::status<std::unique_ptr<FilesystemInstance>> Open(
      const TestFilesystemOptions& options) const override;
  const Traits& GetTraits() const override {
    static Traits traits{
        .name = "minfs",
        .can_unmount = true,
        .timestamp_granularity = zx::nsec(1),
        .supports_hard_links = true,
        .supports_mmap = false,
        .supports_resize = true,
        .max_file_size = minfs::kMinfsMaxFileSize,
        .in_memory = false,
        .is_case_sensitive = true,
        .supports_sparse_files = true,
        .supports_fsck_after_every_transaction = true,
    };
    return traits;
  }
};

// Returns a vector of minfs test options.
std::vector<TestFilesystemOptions> AllTestMinfs();

}  // namespace fs_test

#endif  // SRC_STORAGE_FS_TEST_MINFS_TEST_H_
