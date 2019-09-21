// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <block-client/cpp/fake-device.h>
#include <minfs/fsck.h>
#include <minfs/format.h>
#include <zxtest/zxtest.h>

#include "minfs-private.h"

namespace minfs {
namespace {

using block_client::FakeBlockDevice;

constexpr uint64_t kBlockCount = 1 << 20;
constexpr uint32_t kBlockSize = 512;

class ConsistencyCheckerFixture : public zxtest::Test {
 public:

  void SetUp() override {
    device_ = std::make_unique<FakeBlockDevice>(kBlockCount, kBlockSize);
  }

  std::unique_ptr<FakeBlockDevice> take_device() { return std::move(device_); }

 private:
  std::unique_ptr<FakeBlockDevice> device_;
};

using ConsistencyCheckerTest = ConsistencyCheckerFixture;

TEST_F(ConsistencyCheckerTest, NewlyFormattedFilesystemWithRepair) {
  auto device = take_device();
  std::unique_ptr<Bcache> bcache;
  ASSERT_OK(Bcache::Create(std::move(device), kBlockCount, &bcache));
  ASSERT_OK(Mkfs(bcache.get()));
  ASSERT_OK(Fsck(std::move(bcache), Repair::kEnabled));
}

TEST_F(ConsistencyCheckerTest, NewlyFormattedFilesystemWithoutRepair) {
  auto device = take_device();
  std::unique_ptr<Bcache> bcache;
  ASSERT_OK(Bcache::Create(std::move(device), kBlockCount, &bcache));
  ASSERT_OK(Mkfs(bcache.get()));
  ASSERT_OK(Fsck(std::move(bcache), Repair::kDisabled));
}

TEST_F(ConsistencyCheckerTest, NewlyFormattedFilesystemCheckAfterMount) {
  auto device = take_device();
  std::unique_ptr<Bcache> bcache;
  ASSERT_OK(Bcache::Create(std::move(device), kBlockCount, &bcache));
  ASSERT_OK(Mkfs(bcache.get()));

  Superblock info = {};
  bool writable = true;
  ASSERT_OK(LoadAndUpgradeSuperblockAndJournal(bcache.get(), writable, &info));

  std::unique_ptr<Minfs> fs;
  ASSERT_OK(Minfs::Create(std::move(bcache), &info, IntegrityCheck::kAll, &fs));
  Minfs::DestroyMinfs(std::move(fs), &bcache);
  ASSERT_OK(Fsck(std::move(bcache), Repair::kEnabled));
}

}  // namespace
}  // namespace minfs
