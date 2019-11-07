// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_MINFS_TEST_INTEGRATION_MINFS_FIXTURES_H_
#define ZIRCON_SYSTEM_ULIB_MINFS_TEST_INTEGRATION_MINFS_FIXTURES_H_

#include <fs/test_support/fixtures.h>
#include <minfs/format.h>

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

#endif  // ZIRCON_SYSTEM_ULIB_MINFS_TEST_INTEGRATION_MINFS_FIXTURES_H_
