// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <fuchsia/hardware/block/c/fidl.h>
#include <fuchsia/hardware/block/volume/c/fidl.h>
#include <fuchsia/io/c/fidl.h>
#include <fuchsia/minfs/c/fidl.h>
#include <getopt.h>
#include <lib/fdio/cpp/caller.h>
#include <limits.h>
#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <zircon/device/block.h>
#include <zircon/device/vfs.h>
#include <zircon/syscalls.h>

#include <utility>

#include <fbl/string.h>
#include <fbl/string_buffer.h>
#include <fbl/unique_fd.h>
#include <fs-management/mount.h>
#include <gtest/gtest.h>
#include <ramdevice-client/ramdisk.h>
#include <storage-metrics/block-metrics.h>
#include <storage-metrics/fs-metrics.h>

#include "src/storage/fs_test/fs_test_fixture.h"
#include "src/storage/minfs/format.h"
#include "src/storage/minfs/test/micro-benchmark/block-device-utils.h"
#include "src/storage/minfs/test/micro-benchmark/minfs-costs.h"

namespace minfs_micro_benchmanrk {
namespace {

using MinfsFidlMetrics = fuchsia_minfs_Metrics;

template <const MinfsProperties& fs_properties>
class MinfsMicroBenchmarkFixture : public fs_test::BaseFilesystemTest {
 public:
  static fs_test::TestFilesystemOptions GetOptionsFromProperties(
      const MinfsProperties& properties) {
    fs_test::TestFilesystemOptions options = fs_test::TestFilesystemOptions::MinfsWithoutFvm();
    options.device_block_size = properties.DeviceSizes().block_size;
    options.device_block_count = properties.DeviceSizes().block_count;
    return options;
  }

  MinfsMicroBenchmarkFixture()
      : fs_test::BaseFilesystemTest(GetOptionsFromProperties(fs_properties)) {}

  enum Reset {
    kReset,    // Resets(clears) stats after getting the stats.
    kNoReset,  // Leaves stats unchanged  after getting the stats.
  };
  // Retrieves metrics for the block device at device_. Clears metrics if clear is true.
  void GetBlockMetrics(Reset reset, BlockFidlMetrics* out_stats) const {
    fbl::unique_fd fd(open(fs().DevicePath().value().c_str(), O_RDONLY));
    ASSERT_TRUE(fd);

    fdio_cpp::FdioCaller caller(std::move(fd));
    auto result = llcpp::fuchsia::hardware::block::Block::Call::GetStats(caller.channel(),
                                                                         reset == Reset::kReset);
    ASSERT_EQ(result.status(), ZX_OK);
    *out_stats = *result->stats;
  }

  bool DirtyCacheEnabled() const {
    fbl::unique_fd fd(open(fs().mount_path().c_str(), O_RDONLY | O_DIRECTORY));
    EXPECT_TRUE(fd);

    fdio_cpp::FdioCaller caller(std::move(fd));
    auto mount_state_or = ::llcpp::fuchsia::minfs::Minfs::Call::GetMountState(caller.channel());
    EXPECT_TRUE(mount_state_or.ok());
    EXPECT_EQ(mount_state_or.value().status, ZX_OK);
    EXPECT_NE(mount_state_or.value().mount_state, nullptr);
    return mount_state_or.value().mount_state->dirty_cache_enabled;
  }

  const MinfsProperties& FsProperties() const { return properties_; }

  void CompareAndDump(const BlockFidlMetrics& computed) {
    BlockFidlMetrics from_device;
    GetBlockMetrics(Reset::kNoReset, &from_device);

    storage_metrics::BlockDeviceMetrics device_metrics(&from_device);
    storage_metrics::BlockDeviceMetrics computed_metrics(&computed);
    storage_metrics::BlockStatFidl fidl_device, fidl_computed;
    device_metrics.CopyToFidl(&fidl_device);
    computed_metrics.CopyToFidl(&fidl_computed);

    auto result = storage_metrics::BlockStatEqual(fidl_device, fidl_computed);

    if (!result) {
      fprintf(stdout, "Performance changed. Found\n");
      device_metrics.Dump(stdout, true);
      fprintf(stdout, "Expected\n");
      computed_metrics.Dump(stdout, true);
    }
    ASSERT_TRUE(result);
  }

  void UnmountAndCompareBlockMetrics() {
    if (!Mounted()) {
      return;
    }

    BlockFidlMetrics computed = {}, unused_;
    Sync();
    // Clear block metrics
    GetBlockMetrics(Reset::kReset, &unused_);

    TearDownFs();

    FsProperties().AddUnmountCost(&computed);
    CompareAndDump(computed);
  }

  void SyncAndCompute(BlockFidlMetrics* out, MinfsProperties::SyncKind kind) {
    Sync();
    FsProperties().AddSyncCost(out, kind);
  }

  void LookUpAndCompare(const char* filename, bool failed_lookup) {
    BlockFidlMetrics computed = {}, unused_;
    Sync();
    GetBlockMetrics(Reset::kReset, &unused_);

    struct stat s;
    int result = stat(filename, &s);
    auto err = errno;
    if (failed_lookup) {
      EXPECT_EQ(err, ENOENT);
      EXPECT_EQ(result, -1);
    } else {
      EXPECT_EQ(result, 0);
    }
    SyncAndCompute(&computed, MinfsProperties::SyncKind::kNoTransaction);
    FsProperties().AddLookUpCost(&computed);
    CompareAndDump(computed);
  }

  void CreateAndCompare(const char* filename, fbl::unique_fd* out) {
    BlockFidlMetrics computed = {}, unused_;
    Sync();

    // Clear block metrics
    GetBlockMetrics(Reset::kReset, &unused_);

    fbl::unique_fd fd(open(filename, O_CREAT | O_RDWR));
    EXPECT_GT(fd.get(), 0);
    SyncAndCompute(&computed, MinfsProperties::SyncKind::kTransactionWithNoData);
    FsProperties().AddCreateCost(&computed);
    CompareAndDump(computed);
    *out = std::move(fd);
  }

  void WriteAndCompare(int fd, int bytes_per_write, int write_count) {
    EXPECT_LE(bytes_per_write, static_cast<int>(minfs::kMinfsBlockSize));
    BlockFidlMetrics computed = {}, unused_;
    Sync();
    // Clear block metrics
    GetBlockMetrics(Reset::kReset, &unused_);

    uint8_t ch[bytes_per_write];
    for (int i = 0; i < write_count; i++) {
      EXPECT_EQ(write(fd, ch, sizeof(ch)), static_cast<ssize_t>(sizeof(ch)));
    }
    FsProperties().AddWriteCost(0, bytes_per_write, write_count, DirtyCacheEnabled(), &computed);

    SyncAndCompute(&computed, MinfsProperties::SyncKind::kTransactionWithData);
    CompareAndDump(computed);
  }

  static void SetUpTestCase() {}
  static void TearDownTestCase() {}

  void Sync() { EXPECT_EQ(fsync(root_fd_.get()), 0); }

 protected:
  void SetUp() override { SetUpFs(); }

  void TearDown() override { UnmountAndCompareBlockMetrics(); }

 private:
  mount_options_t GetMountOptions() {
    mount_options_t options = default_mount_options;
    options.register_fs = false;
    return options;
  }

  // Creates a filesystem and mounts it. Clears block metrics after creating the
  // filesystem but before mounting it.
  void SetUpFs() {
    ASSERT_EQ(fs().Unmount().status_value(), ZX_OK);

    minfs::Superblock superblock;
    // We are done with mkfs and are ready to mount the filesystem. Keep a copy
    // of formatted superblock.
    fbl::unique_fd fd(open(fs().DevicePath().value().c_str(), O_RDONLY));
    EXPECT_EQ(read(fd.get(), &superblock, sizeof(superblock)),
              static_cast<ssize_t>(sizeof(superblock)));
    properties_.SetSuperblock(superblock);

    // Clear block metrics after mkfs and verify that they are cleared.
    BlockFidlMetrics metrics;
    GetBlockMetrics(Reset::kReset, &metrics);
    GetBlockMetrics(Reset::kNoReset, &metrics);
    EXPECT_EQ(metrics.read.success.total_calls, 0ul);
    EXPECT_EQ(metrics.read.failure.total_calls, 0ul);
    EXPECT_EQ(metrics.flush.success.total_calls, 0ul);
    EXPECT_EQ(metrics.flush.failure.total_calls, 0ul);
    EXPECT_EQ(metrics.write.success.total_calls, 0ul);
    EXPECT_EQ(metrics.write.failure.total_calls, 0ul);
    EXPECT_EQ(metrics.read.success.bytes_transferred, 0ul);
    EXPECT_EQ(metrics.read.failure.bytes_transferred, 0ul);
    EXPECT_EQ(metrics.write.success.bytes_transferred, 0ul);
    EXPECT_EQ(metrics.write.failure.bytes_transferred, 0ul);
    EXPECT_EQ(metrics.flush.success.bytes_transferred, 0ul);
    EXPECT_EQ(metrics.flush.failure.bytes_transferred, 0ul);

    EXPECT_EQ(fs().Mount().status_value(), ZX_OK);
    mounted_ = true;
    root_fd_ = fbl::unique_fd(open(fs().mount_path().c_str(), O_RDONLY | O_DIRECTORY));
    ASSERT_TRUE(root_fd_);
  }

  bool Mounted() const { return mounted_; }

  void TearDownFs() {
    if (Mounted()) {
      EXPECT_EQ(fs().Unmount().status_value(), ZX_OK);
      mounted_ = false;
    }
  }

  MinfsProperties properties_ = fs_properties;
  bool mounted_ = false;
  fbl::unique_fd root_fd_;
};

constexpr BlockDeviceSizes kDefaultBlockDeviceSizes = {8192, 1 << 13};

constexpr const minfs::Superblock kMinfsZeroedSuperblock = {};
constexpr const mkfs_options_t kMinfsDefaultMkfsOptions = {
    .fvm_data_slices = 1,
    .verbose = false,
};

constexpr MinfsProperties kDefaultMinfsProperties(kDefaultBlockDeviceSizes, DISK_FORMAT_MINFS,
                                                  kMinfsDefaultMkfsOptions, kMinfsZeroedSuperblock);

using MinfsMicroBenchmark = MinfsMicroBenchmarkFixture<kDefaultMinfsProperties>;

TEST_F(MinfsMicroBenchmark, MountCosts) {
  BlockFidlMetrics computed = {};

  // At this time fs is mounted. Check stats.
  FsProperties().AddMountCost(&computed);
  CompareAndDump(computed);
}

TEST_F(MinfsMicroBenchmark, UnmountCosts) { UnmountAndCompareBlockMetrics(); }

TEST_F(MinfsMicroBenchmark, SyncCosts) {
  BlockFidlMetrics computed = {}, unused_;
  Sync();
  // Clear block metrics
  GetBlockMetrics(Reset::kReset, &unused_);
  SyncAndCompute(&computed, MinfsProperties::SyncKind::kNoTransaction);
  CompareAndDump(computed);
}

TEST_F(MinfsMicroBenchmark, LookUpCosts) {
  std::string filename = GetPath("file.txt");
  LookUpAndCompare(filename.c_str(), true);
}

TEST_F(MinfsMicroBenchmark, CreateCosts) {
  std::string filename = GetPath("file.txt");
  fbl::unique_fd unused_fd_;
  CreateAndCompare(filename.c_str(), &unused_fd_);
}

TEST_F(MinfsMicroBenchmark, WriteCosts) {
  std::string filename = GetPath("file.txt");
  fbl::unique_fd fd;
  CreateAndCompare(filename.c_str(), &fd);
  // To write 1 byte, we end up writing 81920 bytes spread over 6 block device
  // write IOs.
  WriteAndCompare(fd.get(), 1, 1);
}

TEST_F(MinfsMicroBenchmark, MultipleWritesWithinOneBlockCosts) {
  std::string filename = GetPath("file.txt");
  fbl::unique_fd fd;
  CreateAndCompare(filename.c_str(), &fd);
  // To write 81 bytes spread over 9 call, we end up writing 540672 bytes spread
  // over 38 block device write IOs.
  WriteAndCompare(fd.get(), 9, 9);
}

TEST_F(MinfsMicroBenchmark, SmallFileMultiBlockWriteCost) {
  std::string filename = GetPath("file.txt");
  fbl::unique_fd fd;
  CreateAndCompare(filename.c_str(), &fd);
  // To write 49152 bytes spread over 6 call, we end up writing 450560 bytes spread
  // over 31 block device write IOs.
  WriteAndCompare(fd.get(), 8192, 6);
}

}  // namespace
}  // namespace minfs_micro_benchmanrk
