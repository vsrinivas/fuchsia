// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_TEST_INTEGRATION_BLOBFS_FIXTURES_H_
#define SRC_STORAGE_BLOBFS_TEST_INTEGRATION_BLOBFS_FIXTURES_H_

#include <blobfs/format.h>

#include "src/storage/blobfs/test/blob_utils.h"
#include "src/storage/fs_test/fs_test_fixture.h"

namespace blobfs {

class BlobfsTest : public fs_test::BaseFilesystemTest {
 public:
  static fs_test::TestFilesystemOptions DefaultOptions() {
    return fs_test::TestFilesystemOptions::BlobfsWithoutFvm();
  }

  explicit BlobfsTest(fs_test::TestFilesystemOptions options = DefaultOptions())
      : fs_test::BaseFilesystemTest(options) {}

  int root_fd() {
    if (!root_fd_) {
      root_fd_.reset(open(fs().mount_path().c_str(), O_DIRECTORY));
    }
    return root_fd_.get();
  }

 private:
  fbl::unique_fd root_fd_;
};

// Base class for tests that create a dedicated disk of a given size.
class BlobfsFixedDiskSizeTest : public BlobfsTest {
 protected:
  static fs_test::TestFilesystemOptions OptionsWithSize(uint64_t disk_size) {
    auto options = fs_test::TestFilesystemOptions::BlobfsWithoutFvm();
    options.device_block_count = disk_size / options.device_block_size;
    return options;
  }
  explicit BlobfsFixedDiskSizeTest(uint64_t disk_size) : BlobfsTest(OptionsWithSize(disk_size)) {}
};

class BlobfsTestWithFvm : public BlobfsTest {
 public:
  BlobfsTestWithFvm() : BlobfsTest(fs_test::TestFilesystemOptions::DefaultBlobfs()) {}
};

// Base class for tests that create a dedicated disk of a given size.
class BlobfsFixedDiskSizeTestWithFvm : public BlobfsTest {
 public:
  static fs_test::TestFilesystemOptions OptionsWithSize(uint64_t disk_size) {
    auto options = fs_test::TestFilesystemOptions::DefaultBlobfs();
    options.device_block_count = disk_size / options.device_block_size;
    return options;
  }
  explicit BlobfsFixedDiskSizeTestWithFvm(uint64_t disk_size)
      : BlobfsTest(OptionsWithSize(disk_size)) {}
};

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_TEST_INTEGRATION_BLOBFS_FIXTURES_H_
