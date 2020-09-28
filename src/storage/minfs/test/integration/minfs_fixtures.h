// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_MINFS_TEST_INTEGRATION_MINFS_FIXTURES_H_
#define SRC_STORAGE_MINFS_TEST_INTEGRATION_MINFS_FIXTURES_H_

#include <fs/test_support/fixtures.h>

#include "src/storage/minfs/format.h"

// FVM slice size used for tests.
constexpr size_t kTestFvmSliceSize = minfs::kMinfsBlockSize * 2;  // 16 KB is the minimum.

constexpr char kMountPath[] = "/minfs-tmp/zircon-minfs-test";

class MinfsTest : public fs::FilesystemTest {
 protected:
  void CheckInfo() override;
};

class MinfsTestWithFvm : public fs::FilesystemTestWithFvm {
 public:
  size_t GetSliceSize() const override { return kTestFvmSliceSize; }

 protected:
  void CheckInfo() override;
  void CheckPartitionSize() override {}
};

#endif  // SRC_STORAGE_MINFS_TEST_INTEGRATION_MINFS_FIXTURES_H_
