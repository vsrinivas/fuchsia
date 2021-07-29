// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "block-device.h"

#include <fcntl.h>
#include <lib/fdio/namespace.h>
#include <lib/syslog/global.h>
#include <lib/syslog/logger.h>
#include <zircon/assert.h>
#include <zircon/hw/gpt.h>

#include <sstream>
#include <string>

#include <cobalt-client/cpp/collector.h>
#include <cobalt-client/cpp/in_memory_logger.h>
#include <cobalt-client/cpp/metric_options.h>
#include <gtest/gtest.h>

#include "src/lib/storage/vfs/cpp/metrics/events.h"
#include "src/storage/fshost/block-device-manager.h"
#include "src/storage/fshost/block-watcher.h"
#include "src/storage/fshost/extract-metadata.h"
#include "src/storage/fshost/filesystem-mounter.h"
#include "src/storage/fshost/fs-manager.h"
#include "src/storage/fshost/metrics_cobalt.h"
#include "src/storage/minfs/format.h"
#include "src/storage/testing/ram_disk.h"

namespace fshost {
namespace {

constexpr uint64_t kBlockSize = 512;
constexpr uint64_t kBlockCount = 1 << 20;

// The class helps in keeping track of number of minfs corruptions seen.
class CorruptionEventCounter : public cobalt_client::InMemoryLogger {
 public:
  explicit CorruptionEventCounter(std::atomic<uint32_t>* corruption_count)
      : cobalt_client::InMemoryLogger(), corruption_count_(corruption_count) {}

  bool Log(const cobalt_client::MetricOptions& metric_info, int64_t count) override {
    auto ret = cobalt_client::InMemoryLogger::Log(metric_info, count);

    // If this happens to be a minfs corruption event then track count.
    if (metric_info.metric_id == static_cast<std::underlying_type<fs_metrics::Event>::type>(
                                     fs_metrics::Event::kDataCorruption) &&
        metric_info.event_codes[0] == static_cast<uint32_t>(fs_metrics::CorruptionSource::kMinfs)) {
      ++(*corruption_count_);
    }
    return ret;
  }

 private:
  std::atomic<uint32_t>* corruption_count_;
};

std::unique_ptr<FsHostMetrics> MakeMetrics(std::atomic<uint32_t>* corruption_count) {
  std::unique_ptr<CorruptionEventCounter> logger_ptr =
      std::make_unique<CorruptionEventCounter>(corruption_count);
  return std::make_unique<FsHostMetricsCobalt>(
      std::make_unique<cobalt_client::Collector>(std::move(logger_ptr)));
}

class BlockDeviceTest : public testing::Test {
 public:
  BlockDeviceTest()
      : manager_(nullptr, MakeMetrics(&minfs_corruption_count_)), watcher_(manager_, &config_) {}

  void SetUp() override {
    // Initialize FilesystemMounter.
    fidl::ServerEnd<fuchsia_io::Directory> dir_request;
    fidl::ServerEnd<fuchsia_process_lifecycle::Lifecycle> lifecycle_request;
    ASSERT_EQ(manager_.Initialize(std::move(dir_request), std::move(lifecycle_request),
                                  zx::channel(), nullptr, watcher_),
              ZX_OK);
    manager_.DisableCrashReporting();

    // Fshost really likes mounting filesystems at "/fs".
    // Let's make that available in our namespace.
    auto root = fidl::CreateEndpoints<fuchsia_io::Directory>();
    ASSERT_EQ(root.status_value(), ZX_OK);
    ASSERT_EQ(manager_.ServeRoot(std::move(root->server)), ZX_OK);
    fdio_ns_t* ns;
    ASSERT_EQ(fdio_ns_get_installed(&ns), ZX_OK);
    ASSERT_EQ(fdio_ns_bind(ns, "/fs", root->client.TakeChannel().release()), ZX_OK);

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
    storage::RamDisk::Options options;
    if (use_guid)
      options.type_guid = std::array<uint8_t, GPT_GUID_LEN>(GUID_DATA_VALUE);
    ramdisk_ = storage::RamDisk::Create(kBlockSize, kBlockCount, options).value();
    ASSERT_EQ(wait_for_device(ramdisk_->path().c_str(), zx::sec(10).get()), ZX_OK);
  }

  fbl::unique_fd GetRamdiskFd() { return fbl::unique_fd(open(ramdisk_->path().c_str(), O_RDWR)); }

  fbl::unique_fd devfs_root() { return fbl::unique_fd(open("/dev", O_RDWR)); }

  uint32_t corruption_count() const { return minfs_corruption_count_.load(); }

 protected:
  FsManager manager_;
  Config config_;

 private:
  // This counts number of minfs corruptions events seen.
  std::atomic<uint32_t> minfs_corruption_count_ = 0;
  std::optional<storage::RamDisk> ramdisk_;
  BlockWatcher watcher_;
};

TEST_F(BlockDeviceTest, TestBadHandleDevice) {
  FilesystemMounter mounter(manager_, &config_);
  fbl::unique_fd fd;
  BlockDevice device(&mounter, {}, &config_);
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

TEST_F(BlockDeviceTest, TestEmptyDevice) {
  FilesystemMounter mounter(manager_, &config_);

  // Initialize Ramdisk.
  ASSERT_NO_FATAL_FAILURE(CreateRamdisk(/*use_guid=*/true));

  BlockDevice device(&mounter, GetRamdiskFd(), &config_);
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

TEST_F(BlockDeviceTest, TestMinfsBadGUID) {
  FilesystemMounter mounter(manager_, &config_);

  // Initialize Ramdisk with an empty GUID.
  ASSERT_NO_FATAL_FAILURE(CreateRamdisk());

  // We started with an empty block device, but let's lie and say it
  // should have been a minfs device.
  BlockDevice device(&mounter, GetRamdiskFd(), &config_);
  device.SetFormat(DISK_FORMAT_MINFS);
  EXPECT_EQ(device.GetFormat(), DISK_FORMAT_MINFS);
  EXPECT_EQ(device.FormatFilesystem(), ZX_OK);

  // Unlike earlier, where we received "ERR_NOT_SUPPORTED", we get "ERR_WRONG_TYPE"
  // because the ramdisk doesn't have a data GUID.
  EXPECT_EQ(device.MountFilesystem(), ZX_ERR_WRONG_TYPE);
}

TEST_F(BlockDeviceTest, TestMinfsGoodGUID) {
  FilesystemMounter mounter(manager_, &config_);

  // Initialize Ramdisk with a data GUID.
  ASSERT_NO_FATAL_FAILURE(CreateRamdisk(true));

  BlockDevice device(&mounter, GetRamdiskFd(), &config_);
  device.SetFormat(DISK_FORMAT_MINFS);
  EXPECT_EQ(device.GetFormat(), DISK_FORMAT_MINFS);
  EXPECT_EQ(device.FormatFilesystem(), ZX_OK);

  EXPECT_EQ(device.MountFilesystem(), ZX_OK);
  EXPECT_EQ(device.MountFilesystem(), ZX_ERR_ALREADY_BOUND);
}

TEST_F(BlockDeviceTest, TestMinfsReformat) {
  Config config(Config::Options{{Config::kCheckFilesystems, {}}});
  FilesystemMounter mounter(manager_, &config);

  // Initialize Ramdisk with a data GUID.
  ASSERT_NO_FATAL_FAILURE(CreateRamdisk(true));

  BlockDevice device(&mounter, GetRamdiskFd(), &config);
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

TEST_F(BlockDeviceTest, TestBlobfs) {
  Config config(Config::Options{{Config::kCheckFilesystems, {}}});
  FilesystemMounter mounter(manager_, &config);

  // Initialize Ramdisk with a data GUID.
  ASSERT_NO_FATAL_FAILURE(CreateRamdisk(true));

  BlockDevice device(&mounter, GetRamdiskFd(), &config);
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

TEST_F(BlockDeviceTest, TestCorruptionEventLogged) {
  Config config(Config::Options{{Config::kCheckFilesystems, {}}});
  FilesystemMounter mounter(manager_, &config);

  // Initialize Ramdisk with a data GUID.
  ASSERT_NO_FATAL_FAILURE(CreateRamdisk(true));

  BlockDevice device(&mounter, GetRamdiskFd(), &config);
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
  while (corruption_count() == 0) {
    sleep(1);
  }
}

std::unique_ptr<std::string> GetData(int fd) {
  constexpr size_t kBufferSize = 10 * 1024 * 1024;
  auto buffer = std::make_unique<char[]>(kBufferSize);
  memset(buffer.get(), 0, kBufferSize);
  ssize_t read_length;
  size_t offset = 0;
  while ((read_length = read(fd, &buffer.get()[offset], kBufferSize - offset - 1)) >= 0) {
    EXPECT_GE(read_length, 0);
    offset += read_length;
    if (offset >= kBufferSize - 1 || read_length == 0) {
      buffer.get()[kBufferSize - 1] = '\0';
      return std::make_unique<std::string>(std::string(buffer.get()));
    }
  }
  EXPECT_GE(read_length, 0);
  return nullptr;
}

std::pair<fbl::unique_fd, fbl::unique_fd> SetupLog() {
  int pipefd[2];
  EXPECT_EQ(pipe2(pipefd, O_NONBLOCK), 0);
  fbl::unique_fd fd_to_close1(pipefd[0]);
  fbl::unique_fd fd_to_close2(pipefd[1]);
  fx_logger_activate_fallback(fx_log_get_logger(), pipefd[0]);

  return {std::move(fd_to_close1), std::move(fd_to_close2)};
}

TEST_F(BlockDeviceTest, ExtractMinfsOnCorruptionToLog) {
  auto fd_pair = SetupLog();
  Config config(Config::Options{{Config::kCheckFilesystems, {}}});
  FilesystemMounter mounter(manager_, &config);

  // Initialize Ramdisk with a data GUID.
  ASSERT_NO_FATAL_FAILURE(CreateRamdisk(true));

  BlockDevice device(&mounter, GetRamdiskFd(), &config);
  device.SetFormat(DISK_FORMAT_MINFS);
  EXPECT_EQ(device.GetFormat(), DISK_FORMAT_MINFS);
  // Format minfs.
  EXPECT_EQ(device.FormatFilesystem(), ZX_OK);

  // Corrupt minfs.
  uint64_t buffer_size = minfs::kMinfsBlockSize * 8;
  std::unique_ptr<uint8_t[]> buffer(new uint8_t[buffer_size]);
  memset(buffer.get(), 0, buffer_size);
  ASSERT_EQ(pread(GetRamdiskFd().get(), buffer.get(), buffer_size, 0),
            static_cast<ssize_t>(buffer_size));
  // Corrupt checksum of both copies of superblocks.
  buffer.get()[offsetof(minfs::Superblock, checksum)] += 1;
  buffer.get()[(minfs::kMinfsBlockSize * 7) + offsetof(minfs::Superblock, checksum)] += 1;
  ASSERT_EQ(pwrite(GetRamdiskFd().get(), buffer.get(), buffer_size, 0),
            static_cast<ssize_t>(buffer_size));

  EXPECT_NE(device.CheckFilesystem(), ZX_OK);
  fd_pair.first.reset();
  auto logs = GetData(fd_pair.second.get());
  auto header_line = logs->find("EIL: Extracting minfs to serial.");
  auto helper_line1 =
      logs->find("EIL: Following lines that start with \"EIL\" are from extractor.");
  auto helper_line2 =
      logs->find("EIL: Successful extraction ends with \"EIL: Done extracting minfs to serial.\"");
  auto dump_option_line =
      logs->find("EIL: Compression:off Checksum:on Offset:on bytes_per_line:64");
  auto offsets_string = logs->find("EIL 0-63:");
  auto checksum_line = logs->find(":checksum: ");

  if (ExtractMetadataEnabled()) {
    ASSERT_NE(header_line, std::string::npos);
    ASSERT_NE(helper_line1, std::string::npos);
    ASSERT_NE(helper_line2, std::string::npos);
    ASSERT_NE(dump_option_line, std::string::npos);
    ASSERT_NE(offsets_string, std::string::npos);
    ASSERT_NE(checksum_line, std::string::npos);
    ASSERT_NE(logs->find("EIL: Done extracting minfs to serial", checksum_line), std::string::npos);
  } else {
    ASSERT_EQ(header_line, std::string::npos);
    ASSERT_EQ(helper_line1, std::string::npos);
    ASSERT_EQ(helper_line2, std::string::npos);
    ASSERT_EQ(dump_option_line, std::string::npos);
    ASSERT_EQ(offsets_string, std::string::npos);
    ASSERT_EQ(checksum_line, std::string::npos);
    ASSERT_EQ(logs->find("EIL: Done extracting minfs to serial"), std::string::npos);
  }
}

// TODO(unknown): Add tests for Zxcrypt binding.

}  // namespace
}  // namespace fshost
