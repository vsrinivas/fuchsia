// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fs/test_support/fixtures.h"

#include <fcntl.h>
#include <fuchsia/device/llcpp/fidl.h>
#include <fuchsia/hardware/block/partition/c/fidl.h>
#include <fuchsia/hardware/block/volume/c/fidl.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <limits.h>

#include <fbl/unique_fd.h>
#include <fs-management/fvm.h>
#include <fvm/format.h>
#include <zxtest/zxtest.h>

namespace fio = ::llcpp::fuchsia::io;

namespace {

constexpr char kFvmDriverLib[] = "/boot/driver/fvm.so";

}  // namespace

namespace fs {

FilesystemTest::FilesystemTest(FsTestType type)
    : type_(type), environment_(g_environment), device_path_(environment_->device_path()) {}

void FilesystemTest::SetUp() {
  srand(zxtest::Runner::GetInstance()->random_seed());
  ASSERT_OK(mkfs(device_path_.c_str(), format_type(), launch_stdio_sync, &default_mkfs_options));
  Mount();
}

void FilesystemTest::TearDown() {
  if (environment_->ramdisk()) {
    environment_->ramdisk()->WakeUp();
  }
  if (mounted_) {
    CheckInfo();  // Failures here should not prevent unmount.
  }
  ASSERT_NO_FAILURES(Unmount());
  ASSERT_OK(CheckFs());
}

void FilesystemTest::Remount() {
  ASSERT_NO_FAILURES(Unmount());
  ASSERT_OK(CheckFs());
  Mount();
}

void FilesystemTest::Mount() {
  ASSERT_FALSE(mounted_);
  int flags = read_only_ ? O_RDONLY : O_RDWR;

  fbl::unique_fd fd(open(device_path_.c_str(), flags));
  ASSERT_TRUE(fd);

  init_options_t options = default_init_options;
  options.enable_journal = environment_->use_journal();
  options.enable_pager = environment_->use_pager();
  if (environment_->write_compression_algorithm()) {
    options.write_compression_algorithm = *environment_->write_compression_algorithm();
  }
  if (environment_->write_compression_level()) {
    options.write_compression_level = *environment_->write_compression_level();
  }

  if (read_only_) {
    options.readonly = true;
  }

  // fd consumed by mount. By default, mount waits until the filesystem is
  // ready to accept commands.
  ASSERT_OK(MountInternal(std::move(fd), mount_path(), format_type(), &options));
  mounted_ = true;
}

zx_status_t FilesystemTest::MountInternal(fbl::unique_fd device_fd, const char* mount_path,
                                          disk_format_t disk_format,
                                          const init_options_t* init_options) {
  zx_status_t status;
  zx::channel device;
  status = fdio_get_service_handle(device_fd.release(), device.reset_and_get_address());
  if (status != ZX_OK) {
    return status;
  }

  // Launch the filesystem process.
  zx::channel export_root;
  status =
      fs_init(device.release(), disk_format, init_options, export_root.reset_and_get_address());
  if (status != ZX_OK) {
    return status;
  }

  // Extract the handle to the root of the filesystem from the export root.
  zx::channel data_root;
  status = fs_root_handle(export_root.get(), data_root.reset_and_get_address());
  if (status != ZX_OK) {
    return status;
  }

  // Mount the data root on |mount_path|.
  zx::channel mount_point, mount_point_server;
  status = zx::channel::create(0, &mount_point, &mount_point_server);
  if (status != ZX_OK) {
    return status;
  }
  status = fdio_open(mount_path, O_RDONLY | O_DIRECTORY | O_ADMIN, mount_point_server.release());
  if (status != ZX_OK) {
    return status;
  }
  fio::DirectoryAdmin::SyncClient mount_client(std::move(mount_point));
  auto resp = mount_client.Mount(std::move(data_root));
  if (resp.status() != ZX_OK) {
    return resp.status();
  }
  if (resp.value().s != ZX_OK) {
    return resp.value().s;
  }

  export_root_ = fio::Directory::SyncClient(std::move(export_root));
  return ZX_OK;
}

void FilesystemTest::Unmount() {
  if (!mounted_) {
    return;
  }

  // Unmount will propagate the result of sync; for cases where the filesystem is disconnected
  // from the underlying device, ZX_ERR_IO_REFUSED is expected.
  zx_status_t status = umount(mount_path());
  ASSERT_TRUE(status == ZX_OK || status == ZX_ERR_IO_REFUSED);
  mounted_ = false;
}

void FilesystemTest::GetFsInfo(::llcpp::fuchsia::io::FilesystemInfo* info) {
  fbl::unique_fd fd(open(mount_path(), O_RDONLY | O_DIRECTORY));
  ASSERT_TRUE(fd);

  fdio_cpp::FdioCaller caller(std::move(fd));
  auto result = ::llcpp::fuchsia::io::DirectoryAdmin::Call::QueryFilesystem(
      zx::unowned_channel(caller.borrow_channel()));
  ASSERT_OK(result.status());
  ASSERT_OK(result.value().s);
  ASSERT_NOT_NULL(result->info);
  *info = *result->info;
}

zx_status_t FilesystemTest::CheckFs() {
  fsck_options_t test_fsck_options = {
      .verbose = false,
      .never_modify = true,
      .always_modify = false,
      .force = true,
      .apply_journal = true,
  };
  return fsck(device_path_.c_str(), format_type(), &test_fsck_options, launch_stdio_sync);
}

void FilesystemTestWithFvm::SetUp() {
  ASSERT_NO_FAILURES(FvmSetUp());
  FilesystemTest::SetUp();
}

void FilesystemTestWithFvm::TearDown() {
  FilesystemTest::TearDown();
  ASSERT_OK(fvm_destroy(partition_path_.c_str()));
}

void FilesystemTestWithFvm::FvmSetUp() {
  fvm_path_.assign(device_path_);
  fvm_path_.append("/fvm");

  ASSERT_NO_FAILURES(CheckPartitionSize());

  CreatePartition();
}

void FilesystemTestWithFvm::BindFvm() {
  fbl::unique_fd fd(open(device_path_.c_str(), O_RDWR));
  ASSERT_TRUE(fd, "Could not open test disk");
  ASSERT_OK(fvm_init(fd.get(), GetSliceSize()));

  fdio_cpp::FdioCaller caller(std::move(fd));
  zx_status_t status;
  auto resp = ::llcpp::fuchsia::device::Controller::Call::Bind(
      zx::unowned_channel(caller.borrow_channel()),
      ::fidl::StringView(kFvmDriverLib, strlen(kFvmDriverLib)));
  status = resp.status();

  ASSERT_OK(status, "Could not send bind to FVM driver");
  if (resp->result.is_err()) {
    status = resp->result.err();
  }
  // TODO(fxbug.dev/39460): Prevent ALREADY_BOUND from being an option
  if (!(status == ZX_OK || status == ZX_ERR_ALREADY_BOUND)) {
    ASSERT_TRUE(false, "Could not bind disk to FVM driver (or failed to find existing bind)");
  }
  ASSERT_OK(wait_for_device(fvm_path_.c_str(), zx::sec(10).get()));
}

void FilesystemTestWithFvm::CreatePartition() {
  ASSERT_NO_FAILURES(BindFvm());

  fbl::unique_fd fd(open(fvm_path_.c_str(), O_RDWR));
  ASSERT_TRUE(fd, "Could not open FVM driver");

  std::string name("fs-test-partition");
  fdio_cpp::FdioCaller caller(std::move(fd));
  auto type = reinterpret_cast<const fuchsia_hardware_block_partition_GUID*>(kTestPartGUID);
  auto guid = reinterpret_cast<const fuchsia_hardware_block_partition_GUID*>(kTestUniqueGUID);
  zx_status_t status;
  zx_status_t io_status = fuchsia_hardware_block_volume_VolumeManagerAllocatePartition(
      caller.borrow_channel(), 1, type, guid, name.c_str(), name.size(), 0, &status);
  ASSERT_OK(io_status, "Could not send message to FVM driver");
  ASSERT_OK(status, "Could not allocate FVM partition");

  std::string path(fvm_path_);
  path.append("/");
  path.append(name);
  path.append("-p-1/block");

  ASSERT_OK(wait_for_device(path.c_str(), zx::sec(10).get()));

  // The base test must see the FVM volume as the device to work with.
  partition_path_.swap(device_path_);
  device_path_.assign(path);
}

FixedDiskSizeTest::FixedDiskSizeTest(uint64_t disk_size) {
  const int kBlockSize = 512;
  uint64_t num_blocks = disk_size / kBlockSize;
  ramdisk_ = std::make_unique<RamDisk>(environment_->devfs_root(), kBlockSize, num_blocks);
  device_path_ = ramdisk_->path();
}

FixedDiskSizeTestWithFvm::FixedDiskSizeTestWithFvm(uint64_t disk_size) {
  const int kBlockSize = 512;
  uint64_t num_blocks = disk_size / kBlockSize;
  ramdisk_ = std::make_unique<RamDisk>(environment_->devfs_root(), kBlockSize, num_blocks);
  device_path_ = ramdisk_->path();
}

void PowerFailureRunner::Run(std::function<void()> function) { Run(function, false); }

void PowerFailureRunner::RunWithRestart(std::function<void()> function) { Run(function, true); }

void PowerFailureRunner::Run(std::function<void()> function, bool restart) {
  const RamDisk* disk = test_->environment()->ramdisk();
  ASSERT_NOT_NULL(disk, "Cannot run against real device");

  ramdisk_block_write_counts_t counts;
  ASSERT_OK(disk->GetBlockCounts(&counts));
  uint64_t mount_count = counts.received;

  function();

  ASSERT_OK(disk->GetBlockCounts(&counts));
  ASSERT_NO_FAILURES(test_->Remount());

  const auto& config = test_->environment()->config();
  uint32_t limit = config.power_cycles ? config.power_cycles
                                       : static_cast<uint32_t>(counts.received - mount_count);

  zx::ticks start_ticks = zx::ticks::now();
  zxtest::LogSink* log = zxtest::Runner::GetInstance()->mutable_reporter()->mutable_log_sink();
  for (uint32_t i = config.power_start; i < limit; i += config.power_stride) {
    log->Write("------------    Test start. Sleep after %d (/ %d) ----------- \n", i, limit);
    ASSERT_OK(disk->SleepAfter(i));
    zxtest::Runner::GetInstance()->DisableAsserts();

    function();
    log->Write("-------------   Test end\n");

    test_->Unmount();

    zxtest::Runner::GetInstance()->EnableAsserts();
    ASSERT_OK(disk->WakeUp());

    log->Write("--- To check fs\n");
    ASSERT_OK(test_->CheckFs());

    if (restart) {
      log->Write("--- To format\n");
      ASSERT_OK(mkfs(test_->device_path().c_str(), test_->format_type(), launch_stdio_sync,
                     &default_mkfs_options));
    }

    ASSERT_NO_FAILURES(test_->Mount());
  }
  log->Write("--- Iteration end! ---\n");

  auto ticks = zx::ticks::now() - start_ticks;
  ticks /= (limit - config.power_start) / config.power_stride;
  auto total_ticks = ticks * (counts.received - mount_count);
  uint64_t minutes = total_ticks / (zx::ticks::per_second() * 60);
  log->Write("--- Test operation count: %lu. Expected time to run full test: %lu minutes\n",
             counts.received - mount_count, minutes);
}

}  // namespace fs
