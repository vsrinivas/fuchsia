// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_TEST_INTEGRATION_BLOBFS_FIXTURES_H_
#define SRC_STORAGE_BLOBFS_TEST_INTEGRATION_BLOBFS_FIXTURES_H_

#include <blobfs/format.h>
#include <fs/test_support/fixtures.h>

#include "test/blob_utils.h"

// FVM slice size used for tests.
constexpr size_t kTestFvmSliceSize = blobfs::kBlobfsBlockSize;  // 8kb.

constexpr char kMountPath[] = "/blobfs-tmp/zircon-blobfs-test";

class BlobfsTest : public fs::FilesystemTest {
 protected:
  void CheckInfo() override;
};

// Base class for tests that create a dedicated disk of a given size.
class BlobfsFixedDiskSizeTest : public fs::FixedDiskSizeTest {
 protected:
  explicit BlobfsFixedDiskSizeTest(uint64_t disk_size) : FixedDiskSizeTest(disk_size) {}
  void CheckInfo() override;
};

class BlobfsTestWithFvm : public fs::FilesystemTestWithFvm {
 public:
  size_t GetSliceSize() const override { return kTestFvmSliceSize; }

 protected:
  void CheckInfo() override;
  void CheckPartitionSize() override;
};

// Base class for tests that create a dedicated disk of a given size.
class BlobfsFixedDiskSizeTestWithFvm : public fs::FixedDiskSizeTestWithFvm {
 public:
  size_t GetSliceSize() const override { return kTestFvmSliceSize; }

 protected:
  explicit BlobfsFixedDiskSizeTestWithFvm(uint64_t disk_size)
      : FixedDiskSizeTestWithFvm(disk_size) {}
  void CheckInfo() override;
};

// Creates an open blob with the provided Merkle tree + Data, and reads back to
// verify the data.
// TODO(rvargas): Move to a better place.
void MakeBlob(const blobfs::BlobInfo* info, fbl::unique_fd* fd);

#endif  // SRC_STORAGE_BLOBFS_TEST_INTEGRATION_BLOBFS_FIXTURES_H_
