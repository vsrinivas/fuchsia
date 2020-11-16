// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_TEST_INTEGRATION_BLOBFS_FIXTURES_H_
#define SRC_STORAGE_BLOBFS_TEST_INTEGRATION_BLOBFS_FIXTURES_H_

#include <blobfs/format.h>

#include "src/storage/blobfs/test/blob_utils.h"
#include "src/storage/fs_test/fs_test_fixture.h"

namespace blobfs {

class BaseBlobfsTest : public fs_test::BaseFilesystemTest {
 public:
  using fs_test::BaseFilesystemTest::BaseFilesystemTest;

  int root_fd() {
    if (!root_fd_) {
      root_fd_.reset(open(fs().mount_path().c_str(), O_DIRECTORY));
    }
    return root_fd_.get();
  }

 private:
  fbl::unique_fd root_fd_;
};

// A test fixture for running tests with different blobfs settings.
class ParameterizedBlobfsTest : public BaseBlobfsTest,
                                public testing::WithParamInterface<fs_test::TestFilesystemOptions> {
 protected:
  ParameterizedBlobfsTest() : BaseBlobfsTest(GetParam()) {}
};

// Different blobfs settings to use with |ParameterizedBlobfsTest|.
fs_test::TestFilesystemOptions BlobfsDefaultTestParam();
fs_test::TestFilesystemOptions BlobfsWithFvmTestParam();
fs_test::TestFilesystemOptions BlobfsWithCompactLayoutTestParam();
fs_test::TestFilesystemOptions BlobfsWithFixedDiskSizeTestParam(uint64_t disk_size);

// A test fixture for tests that only run against blobfs with the default settings.
class BlobfsTest : public BaseBlobfsTest {
 protected:
  explicit BlobfsTest() : BaseBlobfsTest(BlobfsDefaultTestParam()) {}
};

// A test fixture for tests that only run against blobfs with a fixed disk size.
class BlobfsFixedDiskSizeTest : public BaseBlobfsTest {
 protected:
  explicit BlobfsFixedDiskSizeTest(uint64_t disk_size)
      : BaseBlobfsTest(BlobfsWithFixedDiskSizeTestParam(disk_size)) {}
};

// A test fixture for tests that only run against blobfs with FVM.
class BlobfsWithFvmTest : public BaseBlobfsTest {
 protected:
  explicit BlobfsWithFvmTest() : BaseBlobfsTest(BlobfsWithFvmTestParam()) {}
};

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_TEST_INTEGRATION_BLOBFS_FIXTURES_H_
