// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "block-device.h"

#include <fcntl.h>
#include <fidl/fuchsia.io/cpp/wire_test_base.h>
#include <lib/fdio/namespace.h>
#include <lib/fpromise/single_threaded_executor.h>
#include <lib/inspect/cpp/reader.h>
#include <lib/syslog/global.h>
#include <lib/syslog/logger.h>
#include <zircon/assert.h>
#include <zircon/hw/gpt.h>

#include <cstddef>
#include <sstream>
#include <string>

#include <gtest/gtest.h>

#include "fidl/fuchsia.io/cpp/markers.h"
#include "lib/fidl/cpp/wire/internal/transport_channel.h"
#include "src/lib/storage/fs_management/cpp/format.h"
#include "src/lib/storage/fs_management/cpp/mount.h"
#include "src/storage/fshost/block-device-manager.h"
#include "src/storage/fshost/block-watcher.h"
#include "src/storage/fshost/config.h"
#include "src/storage/fshost/extract-metadata.h"
#include "src/storage/fshost/filesystem-mounter.h"
#include "src/storage/fshost/fs-manager.h"
#include "src/storage/minfs/format.h"
#include "src/storage/testing/ram_disk.h"

namespace fshost {
namespace {

constexpr uint64_t kBlockSize = 512;
constexpr uint64_t kBlockCount = 1 << 20;

class BlockDeviceTest : public testing::Test {
 public:
  BlockDeviceTest() : manager_(nullptr), config_(DefaultConfig()), watcher_(manager_, &config_) {}

  void SetUp() override {
    // Initialize FilesystemMounter.
    fidl::ServerEnd<fuchsia_io::Directory> dir_request;
    fidl::ServerEnd<fuchsia_process_lifecycle::Lifecycle> lifecycle_request;
    ASSERT_EQ(manager_.Initialize(std::move(dir_request), std::move(lifecycle_request), config_,
                                  watcher_),
              ZX_OK);
    manager_.DisableCrashReporting();

    // Fshost really likes mounting filesystems at "/fs".
    // Let's make that available in our namespace.
    auto fs_dir_or = manager_.GetFsDir();
    ASSERT_EQ(fs_dir_or.status_value(), ZX_OK);
    fdio_ns_t* ns;
    ASSERT_EQ(fdio_ns_get_installed(&ns), ZX_OK);
    ASSERT_EQ(fdio_ns_bind(ns, "/fs", fs_dir_or->TakeChannel().release()), ZX_OK);

    // fshost uses hardcoded /boot/bin paths to launch filesystems, but this test is packaged now.
    // Make /boot redirect to /pkg in our namespace, which contains the needed binaries.
    int pkg_fd = open("/pkg", O_DIRECTORY | O_RDONLY);
    ASSERT_GE(pkg_fd, 0);
    ASSERT_EQ(fdio_ns_bind_fd(ns, "/boot", pkg_fd), ZX_OK);
    manager_.ReadyForShutdown();
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

  static fbl::unique_fd devfs_root() { return fbl::unique_fd(open("/dev", O_RDWR)); }

 protected:
  FsManager manager_;
  fshost_config::Config config_;

 private:
  std::optional<storage::RamDisk> ramdisk_;
  BlockWatcher watcher_;
};

TEST_F(BlockDeviceTest, TestBadHandleDevice) {
  FilesystemMounter mounter(manager_, &config_);
  fbl::unique_fd fd;
  BlockDevice device(&mounter, {}, &config_);
  EXPECT_EQ(device.GetFormat(), fs_management::kDiskFormatUnknown);
  EXPECT_EQ(device.GetInfo().status_value(), ZX_ERR_BAD_HANDLE);
  fuchsia_hardware_block_partition::wire::Guid null_guid{};
  EXPECT_EQ(memcmp(&device.GetTypeGuid(), &null_guid, sizeof(null_guid)), 0);
  EXPECT_EQ(device.AttachDriver("/foobar"), ZX_ERR_BAD_HANDLE);

  // Returns ZX_OK because zxcrypt currently passes the empty fd to a background
  // thread without observing the results.
  EXPECT_EQ(device.UnsealZxcrypt(), ZX_OK);

  EXPECT_EQ(device.CheckFilesystem(), ZX_ERR_BAD_HANDLE);
  EXPECT_EQ(device.FormatFilesystem(), ZX_ERR_BAD_HANDLE);
  EXPECT_EQ(device.MountFilesystem(), ZX_ERR_BAD_HANDLE);
}

TEST_F(BlockDeviceTest, TestEmptyDevice) {
  FilesystemMounter mounter(manager_, &config_);

  // Initialize Ramdisk.
  ASSERT_NO_FATAL_FAILURE(CreateRamdisk(/*use_guid=*/true));

  BlockDevice device(&mounter, GetRamdiskFd(), &config_);
  EXPECT_EQ(device.GetFormat(), fs_management::kDiskFormatUnknown);
  zx::result info = device.GetInfo();
  EXPECT_EQ(info.status_value(), ZX_OK);
  EXPECT_EQ(info->block_count, kBlockCount);
  EXPECT_EQ(info->block_size, kBlockSize);

  // Black-box: Since we're caching info, double check that re-calling GetInfo
  // works correctly.
  info = device.GetInfo();
  EXPECT_EQ(info.status_value(), ZX_OK);
  EXPECT_EQ(info->block_count, kBlockCount);
  EXPECT_EQ(info->block_size, kBlockSize);

  static constexpr fuchsia_hardware_block_partition::wire::Guid expected_guid = GUID_DATA_VALUE;
  EXPECT_EQ(memcmp(&device.GetTypeGuid(), &expected_guid, sizeof(expected_guid)), 0);

  EXPECT_EQ(device.FormatFilesystem(), ZX_ERR_NOT_SUPPORTED);
  EXPECT_EQ(device.MountFilesystem(), ZX_ERR_NOT_SUPPORTED);
}

class TestMinfsMounter : public FilesystemMounter {
 public:
  TestMinfsMounter(FsManager& fshost, const fshost_config::Config* config)
      : FilesystemMounter(fshost, config) {}

  zx::result<StartedFilesystem> LaunchFs(zx::channel block_device,
                                         const fs_management::MountOptions& options,
                                         fs_management::DiskFormat format) const final {
    EXPECT_EQ(format, fs_management::kDiskFormatMinfs);
    return zx::ok(fs_management::StartedSingleVolumeFilesystem());
  }

  zx::result<> LaunchFsNative(fidl::ServerEnd<fuchsia_io::Directory> server, const char* binary,
                              zx::channel block_device,
                              const fs_management::MountOptions& options) const final {
    ADD_FAILURE() << "Unexpected call to LaunchFsNative";
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  zx_status_t RouteData(fidl::UnownedClientEnd<fuchsia_io::Directory> export_root,
                        std::string_view device_path) override {
    return ZX_OK;
  }
};

TEST_F(BlockDeviceTest, TestMinfsBadGUID) {
  TestMinfsMounter mounter(manager_, &config_);

  // Initialize Ramdisk with an empty GUID.
  ASSERT_NO_FATAL_FAILURE(CreateRamdisk());

  // We started with an empty block device, but let's lie and say it
  // should have been a minfs device.
  BlockDevice device(&mounter, GetRamdiskFd(), &config_);
  device.SetFormat(fs_management::kDiskFormatMinfs);
  EXPECT_EQ(device.GetFormat(), fs_management::kDiskFormatMinfs);
  EXPECT_EQ(device.FormatFilesystem(), ZX_OK);

  // Unlike earlier, where we received "ERR_NOT_SUPPORTED", we get "ERR_WRONG_TYPE"
  // because the ramdisk doesn't have a data GUID.
  EXPECT_EQ(device.MountFilesystem(), ZX_ERR_WRONG_TYPE);
}

TEST_F(BlockDeviceTest, TestMinfsGoodGUID) {
  TestMinfsMounter mounter(manager_, &config_);

  // Initialize Ramdisk with a data GUID.
  ASSERT_NO_FATAL_FAILURE(CreateRamdisk(true));

  BlockDevice device(&mounter, GetRamdiskFd(), &config_);
  device.SetFormat(fs_management::kDiskFormatMinfs);
  EXPECT_EQ(device.GetFormat(), fs_management::kDiskFormatMinfs);
  EXPECT_EQ(device.FormatFilesystem(), ZX_OK);

  EXPECT_EQ(device.MountFilesystem(), ZX_OK);
  EXPECT_EQ(device.MountFilesystem(), ZX_ERR_ALREADY_BOUND);
}

TEST_F(BlockDeviceTest, TestMinfsReformat) {
  auto config = EmptyConfig();
  config.check_filesystems() = true;
  TestMinfsMounter mounter(manager_, &config);

  // Initialize Ramdisk with a data GUID.
  ASSERT_NO_FATAL_FAILURE(CreateRamdisk(true));

  BlockDevice device(&mounter, GetRamdiskFd(), &config);
  device.SetFormat(fs_management::kDiskFormatMinfs);
  EXPECT_EQ(device.GetFormat(), fs_management::kDiskFormatMinfs);

  // Before formatting the device, this isn't a valid minfs partition.
  EXPECT_NE(device.CheckFilesystem(), ZX_OK);

  // After formatting the device, it is a valid partition. We can check the device,
  // and also mount it.
  EXPECT_EQ(device.FormatFilesystem(), ZX_OK);
  EXPECT_EQ(device.CheckFilesystem(), ZX_OK);
  EXPECT_EQ(device.MountFilesystem(), ZX_OK);
}

TEST_F(BlockDeviceTest, TestBlobfs) {
  auto config = EmptyConfig();
  config.check_filesystems() = true;
  FilesystemMounter mounter(manager_, &config);

  // Initialize Ramdisk with a data GUID.
  ASSERT_NO_FATAL_FAILURE(CreateRamdisk(true));

  BlockDevice device(&mounter, GetRamdiskFd(), &config);
  device.SetFormat(fs_management::kDiskFormatBlobfs);
  EXPECT_EQ(device.GetFormat(), fs_management::kDiskFormatBlobfs);

  // Before formatting the device, this isn't a valid blobfs partition.
  // However, as implemented, we always validate the consistency of the filesystem.
  EXPECT_EQ(device.CheckFilesystem(), ZX_OK);

  // Additionally, blobfs does not yet support reformatting within fshost.
  EXPECT_NE(device.FormatFilesystem(), ZX_OK);
  EXPECT_EQ(device.CheckFilesystem(), ZX_OK);
  EXPECT_NE(device.MountFilesystem(), ZX_OK);
}

TEST_F(BlockDeviceTest, TestMinfsCorruptionEventLogged) {
  auto config = EmptyConfig();
  config.check_filesystems() = true;
  FilesystemMounter mounter(manager_, &config);

  // Initialize Ramdisk with a data GUID.
  ASSERT_NO_FATAL_FAILURE(CreateRamdisk(true));

  BlockDevice device(&mounter, GetRamdiskFd(), &config);
  device.SetFormat(fs_management::kDiskFormatMinfs);
  EXPECT_EQ(device.GetFormat(), fs_management::kDiskFormatMinfs);
  // Format minfs.
  EXPECT_EQ(device.FormatFilesystem(), ZX_OK);

  // Corrupt minfs.
  uint64_t buffer_size = static_cast<uint64_t>(minfs::kMinfsBlockSize) * 8;
  std::unique_ptr<uint8_t[]> zeroed_buffer(new uint8_t[buffer_size]);
  memset(zeroed_buffer.get(), 0, buffer_size);
  ASSERT_EQ(write(GetRamdiskFd().get(), zeroed_buffer.get(), buffer_size),
            static_cast<ssize_t>(buffer_size));

  ASSERT_NE(device.CheckFilesystem(), ZX_OK);

  // Verify that we logged a Minfs corruption event to the InspectManager.
  inspect::Hierarchy hierarchy =
      fpromise::run_single_threaded(
          inspect::ReadFromInspector(mounter.inspect_manager().inspector()))
          .take_value();
  const inspect::Hierarchy* corruption_events = hierarchy.GetByPath({"corruption_events"});
  ASSERT_NE(corruption_events, nullptr);
  const auto* property =
      corruption_events->node().get_property<inspect::UintPropertyValue>("minfs");
  ASSERT_NE(property, nullptr);
  ASSERT_EQ(property->value(), 1u);
}

std::unique_ptr<std::string> GetData(int fd) {
  constexpr size_t kBufferSize = 10L * 1024L * 1024L;
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
  auto config = EmptyConfig();
  config.check_filesystems() = true;
  FilesystemMounter mounter(manager_, &config);

  // Initialize Ramdisk with a data GUID.
  ASSERT_NO_FATAL_FAILURE(CreateRamdisk(true));

  BlockDevice device(&mounter, GetRamdiskFd(), &config);
  device.SetFormat(fs_management::kDiskFormatMinfs);
  EXPECT_EQ(device.GetFormat(), fs_management::kDiskFormatMinfs);
  // Format minfs.
  EXPECT_EQ(device.FormatFilesystem(), ZX_OK);

  // Corrupt minfs.
  uint64_t buffer_size = minfs::kMinfsBlockSize * 8LL;
  std::unique_ptr<uint8_t[]> buffer(new uint8_t[buffer_size]);
  memset(buffer.get(), 0, buffer_size);
  ASSERT_EQ(pread(GetRamdiskFd().get(), buffer.get(), buffer_size, 0),
            static_cast<ssize_t>(buffer_size));
  // Corrupt checksum of both copies of superblocks.
  buffer.get()[offsetof(minfs::Superblock, checksum)] += 1;
  buffer.get()[(minfs::kMinfsBlockSize * 7LL) + offsetof(minfs::Superblock, checksum)] += 1;
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
