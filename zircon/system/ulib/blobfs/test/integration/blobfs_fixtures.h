// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_BLOBFS_TEST_INTEGRATION_BLOBFS_FIXTURES_H_
#define ZIRCON_SYSTEM_ULIB_BLOBFS_TEST_INTEGRATION_BLOBFS_FIXTURES_H_

#include <blobfs/format.h>
#include <fs-test-utils/blobfs/blobfs.h>

#include "fixtures.h"

// FVM slice size used for tests.
constexpr size_t kTestFvmSliceSize = blobfs::kBlobfsBlockSize;  // 8kb.

constexpr char kMountPath[] = "/blobfs-tmp/zircon-blobfs-test";

class BlobfsTest : public FilesystemTest {
 protected:
  void CheckInfo() override;
};

class BlobfsTestWithFvm : public FilesystemTestWithFvm {
 public:
  size_t GetSliceSize() const override { return kTestFvmSliceSize; }

 protected:
  void CheckInfo() override;
  void CheckPartitionSize() override;
};

// Creates an open blob with the provided Merkle tree + Data, and reads back to
// verify the data.
// TODO(rvargas): Move to a better place.
void MakeBlob(const fs_test_utils::BlobInfo* info, fbl::unique_fd* fd);

#endif  // ZIRCON_SYSTEM_ULIB_BLOBFS_TEST_INTEGRATION_BLOBFS_FIXTURES_H_
