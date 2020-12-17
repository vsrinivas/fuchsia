// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <block-client/cpp/fake-device.h>
#include <cobalt-client/cpp/in_memory_logger.h>
#include <fs/metrics/events.h>
#include <zxtest/zxtest.h>

#include "src/storage/minfs/format.h"
#include "src/storage/minfs/minfs_private.h"

namespace minfs {
namespace {

using block_client::FakeBlockDevice;

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

  // We have to do things this way because InMemoryLogger is not thread-safe.
  class Logger : public cobalt_client::InMemoryLogger {
   public:
    void Wait() { sync_completion_wait(&sync_, ZX_TIME_INFINITE); }

    bool Log(const cobalt_client::MetricOptions& metric_info, int64_t count) override {
      if (!InMemoryLogger::Log(metric_info, count))
        return false;
      if (metric_info.metric_id == static_cast<uint32_t>(fs_metrics::Event::kVersion)) {
        EXPECT_EQ(metric_info.metric_dimensions, 1);
        EXPECT_EQ(metric_info.event_codes[0], static_cast<uint32_t>(fs_metrics::Component::kMinfs));
        EXPECT_EQ(metric_info.component, std::to_string(kMinfsCurrentFormatVersion) + "/" +
                                             std::to_string(kMinfsCurrentRevision));
        EXPECT_EQ(count, 1);
        sync_completion_signal(&sync_);
      }
      return true;
    }

   private:
    sync_completion_t sync_;
  };

  auto logger = std::make_unique<Logger>();
  auto* logger_ptr = logger.get();
  MountOptions options{.collector_factory = [&logger] {
    return std::make_unique<cobalt_client::Collector>(std::move(logger));
  }};
  std::unique_ptr<Minfs> fs;
  ASSERT_OK(Minfs::Create(std::move(bcache), options, &fs));
  logger_ptr->Wait();
}

}  // namespace
}  // namespace minfs
