// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "block-device.h"

#include <fcntl.h>
#include <lib/fdio/namespace.h>
#include <zircon/assert.h>
#include <zircon/hw/gpt.h>

#include <sstream>

#include <cobalt-client/cpp/collector.h>
#include <cobalt-client/cpp/in_memory_logger.h>
#include <cobalt-client/cpp/metric_options.h>
#include <fs/metrics/events.h>
#include <gtest/gtest.h>

#include "src/lib/isolated_devmgr/v2_component/ram_disk.h"
#include "src/storage/fshost/block-device-manager.h"
#include "src/storage/fshost/block-watcher.h"
#include "src/storage/fshost/filesystem-mounter.h"
#include "src/storage/fshost/fs-manager.h"
#include "src/storage/fshost/metrics.h"
#include "src/storage/minfs/format.h"

namespace devmgr {
namespace {

constexpr uint64_t kBlockSize = 512;
constexpr uint64_t kBlockCount = 1 << 20;

std::unique_ptr<FsHostMetrics> MakeMetrics(cobalt_client::InMemoryLogger** logger) {
  std::unique_ptr<cobalt_client::InMemoryLogger> logger_ptr =
      std::make_unique<cobalt_client::InMemoryLogger>();
  *logger = logger_ptr.get();
  return std::make_unique<FsHostMetrics>(
      std::make_unique<cobalt_client::Collector>(std::move(logger_ptr)));
}

class BlockDeviceHarness : public testing::Test {
 public:
  BlockDeviceHarness()
      : manager_(nullptr, MakeMetrics(&logger_)), watcher_(manager_, FshostOptions()) {}

  void SetUp() override {
    // Initialize FilesystemMounter.
    zx::channel dir_request, lifecycle_request;
    ASSERT_EQ(manager_.Initialize(std::move(dir_request), std::move(lifecycle_request), nullptr,
                                  watcher_),
              ZX_OK);

    // Fshost really likes mounting filesystems at "/fs".
    // Let's make that available in our namespace.
    zx::channel client, server;
    ASSERT_EQ(zx::channel::create(0, &client, &server), ZX_OK);
    ASSERT_EQ(manager_.ServeRoot(std::move(server)), ZX_OK);
    fdio_ns_t* ns;
    ASSERT_EQ(fdio_ns_get_installed(&ns), ZX_OK);
    ASSERT_EQ(fdio_ns_bind(ns, "/fs", client.release()), ZX_OK);
    manager_.WatchExit();

    // fshost uses hardcoded /boot/bin paths to launch filesystems, but this test is packaged now.
    // Make /boot redirect to /pkg in our namespace, which contains the needed binaries.
    int pkg_fd = open("/pkg", O_DIRECTORY | O_RDONLY);
    ASSERT_GE(pkg_fd, 0);
    ASSERT_EQ(fdio_ns_bind_fd(ns, "/boot", pkg_fd), ZX_OK);
  }

  void TearDown() override {
    fdio_ns_t* ns;
    ASSERT_EQ(fdio_ns_get_installed(&ns), ZX_OK);
    fdio_ns_unbind(ns, "/fs");
    fdio_ns_unbind(ns, "/boot");
  }

  void CreateRamdisk(bool use_guid = false) {
    isolated_devmgr::RamDisk::Options options;
    if (use_guid)
      options.type_guid = std::array<uint8_t, GPT_GUID_LEN>(GUID_DATA_VALUE);
    ramdisk_ = isolated_devmgr::RamDisk::Create(kBlockSize, kBlockCount, options).value();
    ASSERT_EQ(wait_for_device(ramdisk_->path().c_str(), zx::sec(10).get()), ZX_OK);
  }

  fbl::unique_fd GetRamdiskFd() { return fbl::unique_fd(open(ramdisk_->path().c_str(), O_RDWR)); }

  fbl::unique_fd devfs_root() { return fbl::unique_fd(open("/dev", O_RDWR)); }

 protected:
  cobalt_client::InMemoryLogger* logger_ = nullptr;
  FsManager manager_;

 private:
  std::optional<isolated_devmgr::RamDisk> ramdisk_;
  BlockWatcher watcher_;
};

TEST_F(BlockDeviceHarness, TestBadHandleDevice) {
  FshostOptions options;
  FilesystemMounter mounter(manager_, options);
  fbl::unique_fd fd;
  BlockDevice device(&mounter, {});
  EXPECT_EQ(device.GetFormat(), DISK_FORMAT_UNKNOWN);
  fuchsia_hardware_block_BlockInfo info;
  EXPECT_EQ(device.GetInfo(&info), ZX_ERR_BAD_HANDLE);
  fuchsia_hardware_block_partition_GUID null_guid{};
  EXPECT_EQ(memcmp(&device.GetTypeGuid(), &null_guid, sizeof(null_guid)), 0);
  EXPECT_EQ(device.AttachDriver("/foobar"), ZX_ERR_BAD_HANDLE);

  // Returns ZX_OK because zxcrypt currently passes the empty fd to a background
  // thread without observing the results.
  EXPECT_EQ(device.UnsealZxcrypt(), ZX_OK);

  // Returns ZX_OK because filesystem checks are disabled.
  EXPECT_EQ(device.CheckFilesystem(), ZX_OK);

  EXPECT_EQ(device.FormatFilesystem(), ZX_ERR_BAD_HANDLE);
  EXPECT_EQ(device.MountFilesystem(), ZX_ERR_BAD_HANDLE);
}

TEST_F(BlockDeviceHarness, TestEmptyDevice) {
  FshostOptions options;
  FilesystemMounter mounter(manager_, options);

  // Initialize Ramdisk.
  ASSERT_NO_FATAL_FAILURE(CreateRamdisk(/*use_guid=*/true));

  BlockDevice device(&mounter, GetRamdiskFd());
  EXPECT_EQ(device.GetFormat(), DISK_FORMAT_UNKNOWN);
  fuchsia_hardware_block_BlockInfo info;
  EXPECT_EQ(device.GetInfo(&info), ZX_OK);
  EXPECT_EQ(info.block_count, kBlockCount);
  EXPECT_EQ(info.block_size, kBlockSize);

  // Black-box: Since we're caching info, double check that re-calling GetInfo
  // works correctly.
  memset(&info, 0, sizeof(info));
  EXPECT_EQ(device.GetInfo(&info), ZX_OK);
  EXPECT_EQ(info.block_count, kBlockCount);
  EXPECT_EQ(info.block_size, kBlockSize);

  static constexpr fuchsia_hardware_block_partition_GUID expected_guid = GUID_DATA_VALUE;
  EXPECT_EQ(memcmp(&device.GetTypeGuid(), &expected_guid, sizeof(expected_guid)), 0);

  EXPECT_EQ(device.FormatFilesystem(), ZX_ERR_NOT_SUPPORTED);
  EXPECT_EQ(device.MountFilesystem(), ZX_ERR_NOT_SUPPORTED);
}

TEST_F(BlockDeviceHarness, TestMinfsBadGUID) {
  FshostOptions options;
  FilesystemMounter mounter(manager_, options);

  // Initialize Ramdisk with an empty GUID.
  ASSERT_NO_FATAL_FAILURE(CreateRamdisk());

  // We started with an empty block device, but let's lie and say it
  // should have been a minfs device.
  BlockDevice device(&mounter, GetRamdiskFd());
  device.SetFormat(DISK_FORMAT_MINFS);
  EXPECT_EQ(device.GetFormat(), DISK_FORMAT_MINFS);
  EXPECT_EQ(device.FormatFilesystem(), ZX_OK);

  // Unlike earlier, where we received "ERR_NOT_SUPPORTED", we get "ERR_WRONG_TYPE"
  // because the ramdisk doesn't have a data GUID.
  EXPECT_EQ(device.MountFilesystem(), ZX_ERR_WRONG_TYPE);
}

TEST_F(BlockDeviceHarness, TestMinfsGoodGUID) {
  FshostOptions options;
  FilesystemMounter mounter(manager_, options);

  // Initialize Ramdisk with a data GUID.
  ASSERT_NO_FATAL_FAILURE(CreateRamdisk(true));

  BlockDevice device(&mounter, GetRamdiskFd());
  device.SetFormat(DISK_FORMAT_MINFS);
  EXPECT_EQ(device.GetFormat(), DISK_FORMAT_MINFS);
  EXPECT_EQ(device.FormatFilesystem(), ZX_OK);

  EXPECT_EQ(device.MountFilesystem(), ZX_OK);
  EXPECT_EQ(device.MountFilesystem(), ZX_ERR_ALREADY_BOUND);
}

TEST_F(BlockDeviceHarness, TestMinfsReformat) {
  FshostOptions options;
  options.check_filesystems = true;
  FilesystemMounter mounter(manager_, options);

  // Initialize Ramdisk with a data GUID.
  ASSERT_NO_FATAL_FAILURE(CreateRamdisk(true));

  BlockDevice device(&mounter, GetRamdiskFd());
  device.SetFormat(DISK_FORMAT_MINFS);
  EXPECT_EQ(device.GetFormat(), DISK_FORMAT_MINFS);

  // Before formatting the device, this isn't a valid minfs partition.
  EXPECT_NE(device.CheckFilesystem(), ZX_OK);
  EXPECT_NE(device.MountFilesystem(), ZX_OK);

  // After formatting the device, it is a valid partition. We can check the device,
  // and also mount it.
  EXPECT_EQ(device.FormatFilesystem(), ZX_OK);
  EXPECT_EQ(device.CheckFilesystem(), ZX_OK);
  EXPECT_EQ(device.MountFilesystem(), ZX_OK);
}

TEST_F(BlockDeviceHarness, TestBlobfs) {
  FshostOptions options;
  options.check_filesystems = true;
  FilesystemMounter mounter(manager_, options);

  // Initialize Ramdisk with a data GUID.
  ASSERT_NO_FATAL_FAILURE(CreateRamdisk(true));

  BlockDevice device(&mounter, GetRamdiskFd());
  device.SetFormat(DISK_FORMAT_BLOBFS);
  EXPECT_EQ(device.GetFormat(), DISK_FORMAT_BLOBFS);

  // Before formatting the device, this isn't a valid blobfs partition.
  // However, as implemented, we always validate the consistency of the filesystem.
  EXPECT_EQ(device.CheckFilesystem(), ZX_OK);
  EXPECT_NE(device.MountFilesystem(), ZX_OK);

  // Additionally, blobfs does not yet support reformatting within fshost.
  EXPECT_NE(device.FormatFilesystem(), ZX_OK);
  EXPECT_EQ(device.CheckFilesystem(), ZX_OK);
  EXPECT_NE(device.MountFilesystem(), ZX_OK);
}

TEST_F(BlockDeviceHarness, TestCorruptionEventLogged) {
  FshostOptions options;
  options.check_filesystems = true;
  FilesystemMounter mounter(manager_, options);

  // Initialize Ramdisk with a data GUID.
  ASSERT_NO_FATAL_FAILURE(CreateRamdisk(true));

  BlockDevice device(&mounter, GetRamdiskFd());
  device.SetFormat(DISK_FORMAT_MINFS);
  EXPECT_EQ(device.GetFormat(), DISK_FORMAT_MINFS);
  // Format minfs.
  EXPECT_EQ(device.FormatFilesystem(), ZX_OK);

  // Corrupt minfs.
  uint64_t buffer_size = minfs::kMinfsBlockSize * 8;
  std::unique_ptr<uint8_t[]> zeroed_buffer(new uint8_t[buffer_size]);
  memset(zeroed_buffer.get(), 0, buffer_size);
  ASSERT_EQ(write(GetRamdiskFd().get(), zeroed_buffer.get(), buffer_size),
            static_cast<ssize_t>(buffer_size));

  EXPECT_NE(device.CheckFilesystem(), ZX_OK);

  // Verify a corruption event was logged.
  cobalt_client::MetricOptions metric_options;
  metric_options.metric_id = static_cast<std::underlying_type<fs_metrics::Event>::type>(
      fs_metrics::Event::kDataCorruption);
  metric_options.event_codes = {static_cast<uint32_t>(fs_metrics::CorruptionSource::kMinfs),
                                static_cast<uint32_t>(fs_metrics::CorruptionType::kMetadata)};
  metric_options.metric_dimensions = 2;
  metric_options.component = {};
  // Block till counters change. Timed sleep without while loop is not sufficient because
  // it make make test flake in virtual environment.
  // The test may timeout and fail if the counter is never seen.
  while (logger_->counters().find(metric_options) == logger_->counters().end()) {
    sleep(1);
  }
  ASSERT_EQ(logger_->counters().at(metric_options), 1ul);
}

TEST(BlockDeviceManager, ReadOptions) {
  std::stringstream stream;
  stream << "# A comment" << std::endl
         << BlockDeviceManager::Options::kDefault << std::endl
         << BlockDeviceManager::Options::kNoZxcrypt
         << std::endl
         // Duplicate keys should be de-duped.
         << BlockDeviceManager::Options::kNoZxcrypt << std::endl
         << BlockDeviceManager::Options::kMinfsMaxBytes << "=1"
         << std::endl
         // Duplicates should overwrite the value.
         << BlockDeviceManager::Options::kMinfsMaxBytes << "=12345"
         << std::endl
         // Empty value.
         << BlockDeviceManager::Options::kBlobfsMaxBytes << "=" << std::endl
         << "-" << BlockDeviceManager::Options::kBlobfs << std::endl
         << "-" << BlockDeviceManager::Options::kFormatMinfsOnCorruption;

  const auto options = BlockDeviceManager::ReadOptions(stream);
  auto expected_options = BlockDeviceManager::DefaultOptions();
  expected_options.options[BlockDeviceManager::Options::kNoZxcrypt] = std::string();
  expected_options.options[BlockDeviceManager::Options::kMinfsMaxBytes] = "12345";
  expected_options.options[BlockDeviceManager::Options::kBlobfsMaxBytes] = std::string();
  expected_options.options.erase(BlockDeviceManager::Options::kBlobfs);
  expected_options.options.erase(BlockDeviceManager::Options::kFormatMinfsOnCorruption);

  EXPECT_EQ(expected_options.options, options.options);
}

// TODO(unknown): Add tests for Zxcrypt binding.

}  // namespace
}  // namespace devmgr
