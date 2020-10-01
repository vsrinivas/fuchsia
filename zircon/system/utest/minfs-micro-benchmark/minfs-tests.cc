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
#include <fs-test-utils/fixture.h>
#include <ramdevice-client/ramdisk.h>
#include <storage-metrics/block-metrics.h>
#include <storage-metrics/fs-metrics.h>
#include <zxtest/zxtest.h>

#include "block-device-utils.h"
#include "minfs-costs.h"
#include "src/storage/minfs/format.h"

namespace minfs_micro_benchmanrk {
namespace {

using MinfsFidlMetrics = fuchsia_minfs_Metrics;

template <typename FsPropertiesType, const FsPropertiesType* fs_properties>
class MinfsMicroBenchmarkFixture : public zxtest::Test {
 public:
  enum Reset {
    kReset,    // Resets(clears) stats after getting the stats.
    kNoReset,  // Leaves stats unchanged  after getting the stats.
  };
  // Retrieves metrics for the block device at device_. Clears metrics if clear is true.
  void GetBlockMetrics(Reset reset, BlockFidlMetrics* out_stats) const {
    fbl::unique_fd fd(open(device_->Path(), O_RDONLY));
    ASSERT_TRUE(fd);

    fdio_cpp::FdioCaller caller(std::move(fd));
    zx_status_t status;
    zx_status_t io_status = fuchsia_hardware_block_BlockGetStats(
        caller.borrow_channel(), reset == Reset::kReset, &status, out_stats);
    ASSERT_OK(io_status);
    ASSERT_OK(status);
  }

  const FsPropertiesType& FsProperties() const { return properties_; }

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
      fprintf(stderr, "Performance changed. Found\n");
      device_metrics.Dump(stderr, true);
      fprintf(stderr, "Expected\n");
      computed_metrics.Dump(stderr, true);
    }
    ASSERT_TRUE(result);
  }

  void UnmountAndCompareBlockMetrics() {
    if (!Mounted()) {
      return;
    }

    BlockFidlMetrics computed = {}, unused_;
    SyncAndCompute(&unused_);
    // Clear block metrics
    GetBlockMetrics(Reset::kReset, &unused_);

    TearDownFs();

    FsProperties().AddUnmountCost(&computed);
    CompareAndDump(computed);
  }

  void SyncAndCompute(BlockFidlMetrics* out, bool update_journal_start = false) {
    Sync();
    FsProperties().AddSyncCost(out, update_journal_start);
  }

  void SyncAndCompare() {
    BlockFidlMetrics computed = {}, unused_;
    SyncAndCompute(&unused_);
    // Clear block metrics
    GetBlockMetrics(Reset::kReset, &unused_);

    SyncAndCompute(&computed);
    CompareAndDump(computed);
  }

  void LookUpAndCompare(const char* filename, bool failed_lookup) {
    BlockFidlMetrics computed = {}, unused_;
    SyncAndCompute(&unused_);
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
    SyncAndCompute(&computed);
    FsProperties().AddLookUpCost(&computed);
    CompareAndDump(computed);
  }

  void CreateAndCompare(const char* filename, fbl::unique_fd* out) {
    BlockFidlMetrics computed = {}, unused_;
    SyncAndCompute(&unused_);

    // Clear block metrics
    GetBlockMetrics(Reset::kReset, &unused_);

    fbl::unique_fd fd(open(filename, O_CREAT | O_RDWR));
    EXPECT_GT(fd.get(), 0);
    SyncAndCompute(&computed, true);
    FsProperties().AddCreateCost(&computed);
    CompareAndDump(computed);
    *out = std::move(fd);
  }

  void WriteAndCompare(int fd) {
    BlockFidlMetrics computed = {}, unused_;
    SyncAndCompute(&unused_);
    // Clear block metrics
    GetBlockMetrics(Reset::kReset, &unused_);

    char ch;
    EXPECT_EQ(write(fd, &ch, sizeof(ch)), 1);
    SyncAndCompute(&computed, true);

    FsProperties().AddWriteCost(0, 1, &computed);
    CompareAndDump(computed);
  }

  static void SetUpTestCase() {}
  static void TearDownTestCase() {}

 protected:
  void SetUp() override {
    device_.reset(new BlockDevice(properties_.DeviceSizes()));
    SetUpFs();
  }

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
    auto path = device_->Path();
    ASSERT_OK(mkfs(path, properties_.DiskFormat(), launch_stdio_sync, &properties_.MkfsOptions()));

    umount(properties_.MountPath());
    unlink(properties_.MountPath());
    EXPECT_EQ(mkdir(properties_.MountPath(), 0666), 0);
    device_fd_.reset(open(path, O_RDWR));
    EXPECT_GT(device_fd_.get(), 0);

    uint8_t superblock_block[minfs::kMinfsBlockSize];
    // We are done with mkfs and are ready to mount the filesystem. Keep a copy
    // of formatted superblock.
    EXPECT_EQ(minfs::kMinfsBlockSize,
              read(device_->BlockFd(), superblock_block, minfs::kMinfsBlockSize));
    properties_.SetSuperblock(*reinterpret_cast<const minfs::Superblock*>(superblock_block));
    fsync(device_fd_.get());

    // Clear block metrics after mkfs and verify that they are cleared.
    BlockFidlMetrics metrics;
    GetBlockMetrics(Reset::kReset, &metrics);
    GetBlockMetrics(Reset::kNoReset, &metrics);
    EXPECT_EQ(metrics.read.success.total_calls, 0);
    EXPECT_EQ(metrics.read.failure.total_calls, 0);
    EXPECT_EQ(metrics.flush.success.total_calls, 0);
    EXPECT_EQ(metrics.flush.failure.total_calls, 0);
    EXPECT_EQ(metrics.write.success.total_calls, 0);
    EXPECT_EQ(metrics.write.failure.total_calls, 0);
    EXPECT_EQ(metrics.read.success.bytes_transferred, 0);
    EXPECT_EQ(metrics.read.failure.bytes_transferred, 0);
    EXPECT_EQ(metrics.write.success.bytes_transferred, 0);
    EXPECT_EQ(metrics.write.failure.bytes_transferred, 0);
    EXPECT_EQ(metrics.flush.success.bytes_transferred, 0);
    EXPECT_EQ(metrics.flush.failure.bytes_transferred, 0);

    const mount_options_t options = GetMountOptions();
    EXPECT_EQ(mount(device_fd_.get(), properties_.MountPath(), properties_.DiskFormat(), &options,
                    launch_stdio_async),
              0);

    root_fd_.reset(open(properties_.MountPath(), O_RDONLY));
    EXPECT_GT(root_fd_.get(), 0);

    mounted_ = true;
  }

  bool Mounted() const { return mounted_; }

  void Sync() { EXPECT_EQ(fsync(root_fd_.get()), 0); }

  void TearDownFs() {
    if (Mounted()) {
      EXPECT_OK(umount(properties_.MountPath()));
      EXPECT_OK(unlink(properties_.MountPath()));
      mounted_ = false;
    }
  }

  FsPropertiesType properties_ = *fs_properties;
  std::unique_ptr<BlockDevice> device_ = {};
  fbl::unique_fd device_fd_;
  fbl::unique_fd root_fd_;
  bool mounted_ = false;
};

constexpr BlockDeviceSizes kDefaultBlockDeviceSizes = {8192, 1 << 13};

constexpr const char kDefaultMinfsMountPath[] = "/memfs/minfs_micro_benchmark_test";
constexpr const minfs::Superblock kMinfsZeroedSuperblock = {};
constexpr const mkfs_options_t kMinfsDefaultMkfsOptions = {
    .fvm_data_slices = 1,
    .verbose = false,
};

const MinfsProperties kDefaultMinfsProperties(kDefaultBlockDeviceSizes, DISK_FORMAT_MINFS,
                                              kMinfsDefaultMkfsOptions, kMinfsZeroedSuperblock,
                                              kDefaultMinfsMountPath);

using MinfsMicroBenchmark = MinfsMicroBenchmarkFixture<MinfsProperties, &kDefaultMinfsProperties>;

TEST_F(MinfsMicroBenchmark, MountCosts) {
  BlockFidlMetrics computed = {};

  // At this time fs is mounted. Check stats.
  FsProperties().AddMountCost(&computed);
  CompareAndDump(computed);
}

TEST_F(MinfsMicroBenchmark, UnmountCosts) { UnmountAndCompareBlockMetrics(); }

TEST_F(MinfsMicroBenchmark, SyncCosts) { SyncAndCompare(); }

TEST_F(MinfsMicroBenchmark, LookUpCosts) {
  std::string filename = FsProperties().MountPath();
  filename.append("/file.txt");
  LookUpAndCompare(filename.c_str(), true);
}

TEST_F(MinfsMicroBenchmark, CreateCosts) {
  std::string filename = FsProperties().MountPath();
  filename.append("/file.txt");
  fbl::unique_fd unused_fd_;
  CreateAndCompare(filename.c_str(), &unused_fd_);
}

TEST_F(MinfsMicroBenchmark, WriteCosts) {
  std::string filename = FsProperties().MountPath();
  filename.append("/file.txt");
  fbl::unique_fd fd;
  CreateAndCompare(filename.c_str(), &fd);
  WriteAndCompare(fd.get());
}

}  // namespace
}  // namespace minfs_micro_benchmanrk

int main(int argc, char** argv) {
  return fs_test_utils::RunWithMemFs([argc, argv]() { return RUN_ALL_TESTS(argc, argv); });
}
