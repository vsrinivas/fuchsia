// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FS_TEST_FXFS_H_
#define SRC_STORAGE_FS_TEST_FXFS_H_

#include "src/storage/fs_test/fs_test.h"

namespace fs_test {

class FxfsFilesystem : public FilesystemImplWithDefaultMake<FxfsFilesystem> {
 public:
  const Traits& GetTraits() const override {
    static Traits traits{
        .name = "fxfs",
        .can_unmount = true,
        .timestamp_granularity = zx::nsec(1),
        .supports_hard_links = false,
        .supports_mmap = false,
        .supports_resize = true,
        // Technically, Fxfs's maximum file size is higher than this, but POSIX APIs take off_t, so
        // we limit it to that, which is plenty.
        .max_file_size = std::numeric_limits<off_t>::max(),
        .in_memory = false,
        .is_case_sensitive = true,
        .supports_sparse_files = true,
        .supports_fsck_after_every_transaction = false,
    };
    return traits;
  }

  std::unique_ptr<FilesystemInstance> Create(RamDevice device,
                                             std::string device_path) const override;

  zx::status<std::unique_ptr<FilesystemInstance>> Open(
      const TestFilesystemOptions& options) const override;
};

TestFilesystemOptions DefaultFxfsTestOptions();

}  // namespace fs_test

#endif  // SRC_STORAGE_FS_TEST_FXFS_H_
