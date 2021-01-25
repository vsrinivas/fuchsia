// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <block-client/cpp/fake-device.h>
#include <cobalt-client/cpp/in_memory_logger.h>
#include <fs/metrics/events.h>
#include <zxtest/zxtest.h>

#include "src/lib/cobalt/cpp/testing/mock_cobalt_logger.h"
#include "src/storage/minfs/format.h"
#include "src/storage/minfs/minfs_private.h"

namespace minfs {
namespace {

using ::block_client::FakeBlockDevice;

constexpr uint64_t kBlockCount = 1 << 15;
constexpr uint32_t kBlockSize = 512;

TEST(MountTest, OldestRevisionUpdatedOnMount) {
  auto device = std::make_unique<FakeBlockDevice>(kBlockCount, kBlockSize);
  std::unique_ptr<Bcache> bcache;
  ASSERT_OK(Bcache::Create(std::move(device), kBlockCount, &bcache));
  ASSERT_OK(Mkfs(bcache.get()));
  Superblock superblock = {};
  ASSERT_OK(LoadSuperblock(bcache.get(), &superblock));

  ASSERT_EQ(kMinfsCurrentRevision, superblock.oldest_revision);

  superblock.oldest_revision = kMinfsCurrentRevision + 1;
  UpdateChecksum(&superblock);
  ASSERT_OK(bcache->Writeblk(kSuperblockStart, &superblock));
  ASSERT_OK(LoadSuperblock(bcache.get(), &superblock));
  ASSERT_EQ(kMinfsCurrentRevision + 1, superblock.oldest_revision);

  MountOptions options = {};
  std::unique_ptr<Minfs> fs;
  ASSERT_OK(Minfs::Create(std::move(bcache), options, &fs));
  bcache = Minfs::Destroy(std::move(fs));

  ASSERT_OK(LoadSuperblock(bcache.get(), &superblock));
  ASSERT_EQ(kMinfsCurrentRevision, superblock.oldest_revision);
}

TEST(MountTest, VersionLoggedWithCobalt) {
  auto device = std::make_unique<FakeBlockDevice>(kBlockCount, kBlockSize);
  std::unique_ptr<Bcache> bcache;
  ASSERT_OK(Bcache::Create(std::move(device), kBlockCount, &bcache));
  ASSERT_OK(Mkfs(bcache.get()));

  class Logger : public cobalt::MockCobaltLogger {
    using MockCobaltLogger::MockCobaltLogger;

    void LogEventCount(uint32_t metric_id, uint32_t event_code, const std::string& component,
                       zx::duration period_duration, int64_t count) override {
      MockCobaltLogger::LogEventCount(metric_id, event_code, component, period_duration, count);
      EXPECT_EQ(metric_id, static_cast<uint32_t>(fs_metrics::Event::kVersion));
      EXPECT_EQ(event_code, static_cast<uint32_t>(fs_metrics::Component::kMinfs));
      EXPECT_EQ(component, std::to_string(kMinfsCurrentFormatVersion) + "/" +
                               std::to_string(kMinfsCurrentRevision));
      EXPECT_EQ(period_duration, zx::duration());
      EXPECT_EQ(count, 1);
    }
  };
  cobalt::CallCountMap call_counts;
  MountOptions options{.cobalt_factory = [&] { return std::make_unique<Logger>(&call_counts); }};
  {
    std::unique_ptr<Minfs> fs;
    ASSERT_OK(Minfs::Create(std::move(bcache), options, &fs));
  }
  auto iter = call_counts.find(cobalt::LogMethod::kLogEventCount);
  ASSERT_NE(iter, call_counts.end());
  EXPECT_EQ(iter->second, 1);
}

}  // namespace
}  // namespace minfs
