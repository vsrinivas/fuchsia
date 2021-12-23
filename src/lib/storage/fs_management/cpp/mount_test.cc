// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <fidl/fuchsia.io.admin/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <fuchsia/hardware/block/c/fidl.h>
#include <fuchsia/hardware/block/volume/c/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/namespace.h>
#include <lib/fit/defer.h>
#include <lib/memfs/memfs.h>
#include <lib/syslog/cpp/macros.h>
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

#include <fbl/unique_fd.h>
#include <fs-management/fvm.h>
#include <fs-management/mount.h>
#include <gtest/gtest.h>
#include <ramdevice-client/ramdisk.h>

#include "src/lib/fxl/test/test_settings.h"
#include "src/storage/fvm/format.h"
#include "src/storage/testing/fvm.h"
#include "src/storage/testing/ram_disk.h"

namespace fs_management {
namespace {

namespace fio = fuchsia_io;

void CheckMountedFs(const char* path, const char* fs_name, size_t len) {
  fbl::unique_fd fd(open(path, O_RDONLY | O_DIRECTORY));
  ASSERT_TRUE(fd);

  fdio_cpp::FdioCaller caller(std::move(fd));
  auto result = fidl::WireCall(fidl::UnownedClientEnd<fuchsia_io_admin::DirectoryAdmin>(
                                   caller.borrow_channel()))
                    ->QueryFilesystem();
  ASSERT_EQ(result.status(), ZX_OK);
  ASSERT_EQ(result->s, ZX_OK);
  fuchsia_io_admin::wire::FilesystemInfo info = *result.value().info;
  ASSERT_EQ(strncmp(fs_name, reinterpret_cast<char*>(info.name.data()), strlen(fs_name)), 0);
  ASSERT_LE(info.used_nodes, info.total_nodes) << "Used nodes greater than free nodes";
  ASSERT_LE(info.used_bytes, info.total_bytes) << "Used bytes greater than free bytes";
  // TODO(planders): eventually check that total/used counts are > 0
}

TEST(MountRemountCase, MountRemount) {
  const char* mount_path = "/test/mount_remount";

  ASSERT_EQ(storage::WaitForRamctl().status_value(), ZX_OK);
  ramdisk_client_t* ramdisk = nullptr;
  ASSERT_EQ(ramdisk_create(512, 1 << 16, &ramdisk), ZX_OK);

  const char* ramdisk_path = ramdisk_get_path(ramdisk);
  ASSERT_EQ(Mkfs(ramdisk_path, kDiskFormatMinfs, launch_stdio_sync, MkfsOptions()), ZX_OK);

  // We should still be able to mount and unmount the filesystem multiple times
  for (size_t i = 0; i < 10; i++) {
    fbl::unique_fd fd(open(ramdisk_path, O_RDWR));
    ASSERT_TRUE(fd);
    auto export_root_or =
        Mount(std::move(fd), mount_path, kDiskFormatMinfs, MountOptions(), launch_stdio_async);
    ASSERT_EQ(export_root_or.status_value(), ZX_OK);
  }
  ASSERT_EQ(ramdisk_destroy(ramdisk), 0);
}

TEST(MountFsckCase, MountFsck) {
  const char* mount_path = "/test/mount_fsck";

  ASSERT_EQ(storage::WaitForRamctl().status_value(), ZX_OK);
  ramdisk_client_t* ramdisk = nullptr;
  ASSERT_EQ(ramdisk_create(512, 1 << 16, &ramdisk), ZX_OK);

  const char* ramdisk_path = ramdisk_get_path(ramdisk);
  ASSERT_EQ(Mkfs(ramdisk_path, kDiskFormatMinfs, launch_stdio_sync, MkfsOptions()), ZX_OK);

  fbl::unique_fd fd(open(ramdisk_path, O_RDWR));
  ASSERT_TRUE(fd) << "Could not open ramdisk device";

  auto mounted_filesystem_or =
      Mount(std::move(fd), mount_path, kDiskFormatMinfs, MountOptions(), launch_stdio_async);
  ASSERT_EQ(mounted_filesystem_or.status_value(), ZX_OK);
  ASSERT_EQ(std::move(*mounted_filesystem_or).Unmount().status_value(), ZX_OK);

  // Fsck shouldn't require any user input for a newly mkfs'd filesystem.
  ASSERT_EQ(Fsck(ramdisk_path, kDiskFormatMinfs, FsckOptions(), launch_stdio_sync), ZX_OK);
  ASSERT_EQ(ramdisk_destroy(ramdisk), 0);
}

TEST(MountGetDeviceCase, MountGetDevice) {
  // First test with memfs.
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_EQ(loop.StartThread("mountest-mount-get-device"), ZX_OK);
  memfs_filesystem_t* memfs;
  ASSERT_EQ(memfs_install_at(loop.dispatcher(), "/memfs", &memfs), ZX_OK);

  auto defer = fit::defer([memfs, &loop] {
    loop.Shutdown();
    memfs_uninstall_unsafe(memfs, "/memfs");
  });

  constexpr const char* kTestDir = "/memfs/mount_get_device";
  ASSERT_EQ(mkdir(kTestDir, 0666), 0);
  CheckMountedFs(kTestDir, "memfs", strlen("memfs"));
  fbl::unique_fd mountfd(open(kTestDir, O_RDONLY | O_ADMIN));
  ASSERT_TRUE(mountfd);
  fdio_cpp::FdioCaller caller(std::move(mountfd));
  auto result = fidl::WireCall(fidl::UnownedClientEnd<fuchsia_io_admin::DirectoryAdmin>(
                                   caller.borrow_channel()))
                    ->GetDevicePath();
  ASSERT_EQ(result.status(), ZX_OK);

  // Memfs does not support GetDevicePath.
  ASSERT_EQ(result->s, ZX_ERR_NOT_SUPPORTED);

  // Now test with minfs.
  ASSERT_EQ(storage::WaitForRamctl().status_value(), ZX_OK);
  ramdisk_client_t* ramdisk = nullptr;
  ASSERT_EQ(ramdisk_create(512, 1 << 16, &ramdisk), ZX_OK);

  const char* ramdisk_path = ramdisk_get_path(ramdisk);
  ASSERT_EQ(Mkfs(ramdisk_path, kDiskFormatMinfs, launch_stdio_sync, MkfsOptions()), ZX_OK);

  fbl::unique_fd fd(open(ramdisk_path, O_RDWR));
  ASSERT_TRUE(fd);

  constexpr const char* kMinfsMountPath = "/test/mount_get_device";
  {
    auto mounted_filesystem_or =
        Mount(std::move(fd), kMinfsMountPath, kDiskFormatMinfs, MountOptions(), launch_stdio_async);
    ASSERT_EQ(mounted_filesystem_or.status_value(), ZX_OK);

    CheckMountedFs(kMinfsMountPath, "minfs", strlen("minfs"));

    mountfd.reset(open(kMinfsMountPath, O_RDONLY | O_ADMIN));
    ASSERT_TRUE(mountfd);
    caller.reset(std::move(mountfd));
    auto result2 = fidl::WireCall(fidl::UnownedClientEnd<fuchsia_io_admin::DirectoryAdmin>(
                                      caller.borrow_channel()))
                       ->GetDevicePath();
    ASSERT_EQ(result2.status(), ZX_OK);
    ASSERT_EQ(result2->s, ZX_OK);
    ASSERT_GT(result2.value().path.size(), 0ul) << "Device path not found";
    ASSERT_STREQ(ramdisk_path, result2.value().path.data()) << "Unexpected device path";

    mountfd.reset(open(kMinfsMountPath, O_RDONLY));
    ASSERT_TRUE(mountfd);
    caller.reset(std::move(mountfd));
    auto result3 = fidl::WireCall(fidl::UnownedClientEnd<fuchsia_io_admin::DirectoryAdmin>(
                                      caller.borrow_channel()))
                       ->GetDevicePath();
    ASSERT_EQ(result3.status(), ZX_OK);
    ASSERT_EQ(result3->s, ZX_ERR_ACCESS_DENIED);
  }

  ASSERT_EQ(ramdisk_destroy(ramdisk), 0);
}

// Mounts a minfs formatted partition to the desired point.
zx::status<fs_management::MountedFilesystem> MountMinfs(fbl::unique_fd block_fd, bool read_only,
                                                        const char* mount_path) {
  MountOptions options;
  options.readonly = read_only;

  auto mounted_filesystem_or =
      Mount(std::move(block_fd), mount_path, kDiskFormatMinfs, options, launch_stdio_async);
  if (mounted_filesystem_or.is_error())
    return mounted_filesystem_or.take_error();
  CheckMountedFs(mount_path, "minfs", strlen("minfs"));
  return mounted_filesystem_or;
}

// Formats the ramdisk with minfs, and writes a small file to it.
void CreateTestFile(const char* ramdisk_path, const char* mount_path, const char* file_name) {
  ASSERT_EQ(Mkfs(ramdisk_path, kDiskFormatMinfs, launch_stdio_sync, MkfsOptions()), ZX_OK);

  fbl::unique_fd fd(open(ramdisk_path, O_RDWR));
  ASSERT_TRUE(fd);
  auto mounted_filesystem_or = MountMinfs(std::move(fd), /*read_only=*/false, mount_path);
  ASSERT_EQ(mounted_filesystem_or.status_value(), ZX_OK);

  fbl::unique_fd root_fd(open(mount_path, O_RDONLY | O_DIRECTORY));
  ASSERT_TRUE(root_fd);
  fd.reset(openat(root_fd.get(), file_name, O_CREAT | O_RDWR));
  ASSERT_TRUE(fd);
  ASSERT_EQ(write(fd.get(), "hello", 6), 6);
}

// Tests that setting read-only on the mount options works as expected.
TEST(MountReadonlyCase, MountReadonly) {
  const char* mount_path = "/test/mount_readonly";
  const char file_name[] = "some_file";

  ASSERT_EQ(storage::WaitForRamctl().status_value(), ZX_OK);
  ramdisk_client_t* ramdisk = nullptr;
  ASSERT_EQ(ramdisk_create(512, 1 << 16, &ramdisk), ZX_OK);
  const char* ramdisk_path = ramdisk_get_path(ramdisk);
  CreateTestFile(ramdisk_path, mount_path, file_name);

  fbl::unique_fd fd(open(ramdisk_path, O_RDWR));
  ASSERT_TRUE(fd);

  bool read_only = true;
  {
    auto mounted_filesystem_or = MountMinfs(std::move(fd), read_only, mount_path);
    ASSERT_EQ(mounted_filesystem_or.status_value(), ZX_OK);

    int root_fd = open(mount_path, O_RDONLY | O_DIRECTORY);
    ASSERT_GE(root_fd, 0);
    fd.reset(openat(root_fd, file_name, O_CREAT | O_RDWR));

    // We can no longer open the file as writable
    ASSERT_FALSE(fd);

    // We CAN open it as readable though
    fd.reset(openat(root_fd, file_name, O_RDONLY));
    ASSERT_TRUE(fd);
    ASSERT_LT(write(fd.get(), "hello", 6), 0);
    char buf[6];
    ASSERT_EQ(read(fd.get(), buf, 6), 6);
    ASSERT_EQ(memcmp(buf, "hello", 6), 0);

    ASSERT_LT(renameat(root_fd, file_name, root_fd, "new_file"), 0);
    ASSERT_LT(unlinkat(root_fd, file_name, 0), 0);

    ASSERT_EQ(close(root_fd), 0);
  }

  ASSERT_EQ(ramdisk_destroy(ramdisk), 0);
}

// Test that when a block device claims to be read-only, the filesystem is mounted as read-only.
TEST(MountBlockReadonlyCase, MountBlockReadonly) {
  const char* mount_path = "/test/mount_readonly";
  const char file_name[] = "some_file";

  ASSERT_EQ(storage::WaitForRamctl().status_value(), ZX_OK);
  ramdisk_client_t* ramdisk = nullptr;
  ASSERT_EQ(ramdisk_create(512, 1 << 16, &ramdisk), ZX_OK);
  const char* ramdisk_path = ramdisk_get_path(ramdisk);
  CreateTestFile(ramdisk_path, mount_path, file_name);

  uint32_t flags = BLOCK_FLAG_READONLY;
  ASSERT_EQ(ramdisk_set_flags(ramdisk, flags), ZX_OK);

  bool read_only = false;
  {
    auto mounted_filesystem_or =
        MountMinfs(fbl::unique_fd(ramdisk_get_block_fd(ramdisk)), read_only, mount_path);
    ASSERT_EQ(mounted_filesystem_or.status_value(), ZX_OK);

    // We can't modify the file.
    int root_fd = open(mount_path, O_RDONLY | O_DIRECTORY);
    ASSERT_GE(root_fd, 0);
    int fd = openat(root_fd, file_name, O_CREAT | O_RDWR);
    ASSERT_LT(fd, 0);

    // We can open it as read-only.
    fd = openat(root_fd, file_name, O_RDONLY);
    ASSERT_GT(fd, 0);
    ASSERT_EQ(close(fd), 0);
    ASSERT_EQ(close(root_fd), 0);
  }

  ASSERT_EQ(ramdisk_destroy(ramdisk), 0);
}

TEST(StatfsTestCase, StatfsTest) {
  const char* mount_path = "/test/mount_unmount";

  ASSERT_EQ(storage::WaitForRamctl().status_value(), ZX_OK);
  ramdisk_client_t* ramdisk = nullptr;
  ASSERT_EQ(ramdisk_create(512, 1 << 16, &ramdisk), ZX_OK);

  const char* ramdisk_path = ramdisk_get_path(ramdisk);
  ASSERT_EQ(Mkfs(ramdisk_path, kDiskFormatMinfs, launch_stdio_sync, MkfsOptions()), ZX_OK);

  fbl::unique_fd fd(open(ramdisk_path, O_RDWR));
  ASSERT_TRUE(fd);

  {
    auto mounted_filesystem_or =
        Mount(std::move(fd), mount_path, kDiskFormatMinfs, MountOptions(), launch_stdio_async);
    ASSERT_EQ(mounted_filesystem_or.status_value(), ZX_OK);

    struct statfs stats;
    int rc = statfs("", &stats);
    int err = errno;
    ASSERT_EQ(rc, -1);
    ASSERT_EQ(err, ENOENT);

    rc = statfs(mount_path, &stats);
    ASSERT_EQ(rc, 0);

    // Verify that at least some values make sense, without making the test too brittle.
    ASSERT_EQ(stats.f_type, VFS_TYPE_MINFS);
    ASSERT_NE(stats.f_fsid.__val[0] | stats.f_fsid.__val[1], 0);
    ASSERT_EQ(stats.f_bsize, 8192u);
    ASSERT_EQ(stats.f_namelen, 255u);
    ASSERT_GT(stats.f_bavail, 0u);
    ASSERT_GT(stats.f_ffree, 0u);
  }

  ASSERT_EQ(ramdisk_destroy(ramdisk), 0);
}

TEST(StatvfsTestCase, StatvfsTest) {
  const char* mount_path = "/test/mount_unmount";

  ASSERT_EQ(storage::WaitForRamctl().status_value(), ZX_OK);
  ramdisk_client_t* ramdisk = nullptr;
  ASSERT_EQ(ramdisk_create(512, 1 << 16, &ramdisk), ZX_OK);

  const char* ramdisk_path = ramdisk_get_path(ramdisk);
  ASSERT_EQ(Mkfs(ramdisk_path, kDiskFormatMinfs, launch_stdio_sync, MkfsOptions()), ZX_OK);

  fbl::unique_fd fd(open(ramdisk_path, O_RDWR));
  ASSERT_TRUE(fd);

  {
    auto mounted_filesystem_or =
        Mount(std::move(fd), mount_path, kDiskFormatMinfs, MountOptions(), launch_stdio_async);
    ASSERT_EQ(mounted_filesystem_or.status_value(), ZX_OK);

    struct statvfs stats;
    int rc = statvfs("", &stats);
    int err = errno;
    ASSERT_EQ(rc, -1);
    ASSERT_EQ(err, ENOENT);

    rc = statvfs(mount_path, &stats);
    ASSERT_EQ(rc, 0);

    // Verify that at least some values make sense, without making the test too brittle.
    ASSERT_NE(stats.f_fsid, 0ul);
    ASSERT_EQ(stats.f_bsize, 8192u);
    ASSERT_EQ(stats.f_frsize, 8192u);
    ASSERT_EQ(stats.f_namemax, 255u);
    ASSERT_GT(stats.f_bavail, 0u);
    ASSERT_GT(stats.f_ffree, 0u);
    ASSERT_GT(stats.f_favail, 0u);
  }

  ASSERT_EQ(ramdisk_destroy(ramdisk), 0);
}

void GetPartitionSliceCount(const zx::unowned_channel& channel, size_t* out_count) {
  fuchsia_hardware_block_volume_VolumeManagerInfo fvm_info;
  fuchsia_hardware_block_volume_VolumeInfo volume_info;
  zx_status_t status;
  ASSERT_EQ(fuchsia_hardware_block_volume_VolumeGetVolumeInfo(channel->get(), &status, &fvm_info,
                                                              &volume_info),
            ZX_OK);
  ASSERT_EQ(status, ZX_OK);

  size_t allocated_slices = 0;
  uint64_t start_slices[1];
  start_slices[0] = 0;
  while (start_slices[0] < fvm_info.max_virtual_slice) {
    fuchsia_hardware_block_volume_VsliceRange
        ranges[fuchsia_hardware_block_volume_MAX_SLICE_REQUESTS];
    size_t actual_ranges_count;
    ASSERT_EQ(fuchsia_hardware_block_volume_VolumeQuerySlices(channel->get(), start_slices,
                                                              std::size(start_slices), &status,
                                                              ranges, &actual_ranges_count),
              ZX_OK);
    ASSERT_EQ(status, ZX_OK);
    ASSERT_EQ(actual_ranges_count, 1ul);
    start_slices[0] += ranges[0].count;
    if (ranges[0].allocated) {
      allocated_slices += ranges[0].count;
    }
  }

  // The two methods of getting the partition slice count should agree.
  EXPECT_EQ(volume_info.partition_slice_count, allocated_slices);

  *out_count = allocated_slices;
}

class PartitionOverFvmWithRamdiskFixture : public testing::Test {
 public:
  const char* partition_path() const { return partition_path_.c_str(); }

 protected:
  static constexpr uint64_t kBlockSize = 512;

  void SetUp() override {
    ASSERT_EQ(storage::WaitForRamctl().status_value(), ZX_OK);
    size_t ramdisk_block_count = zx_system_get_physmem() / (1024);
    ASSERT_EQ(ramdisk_create(kBlockSize, ramdisk_block_count, &ramdisk_), ZX_OK);

    std::string ramdisk_path(ramdisk_get_path(ramdisk_));
    uint64_t slice_size = kBlockSize * (2 << 10);

    fbl::unique_fd ramdisk_fd(open(ramdisk_path.c_str(), O_RDWR));
    ASSERT_TRUE(ramdisk_fd.is_valid()) << strerror(errno);

    storage::FvmOptions options;
    options.name = "my-fake-partition";
    options.type = {15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0};

    auto partition_or =
        storage::CreateFvmPartition(ramdisk_path, static_cast<int>(slice_size), options);
    ASSERT_TRUE(partition_or.is_ok()) << partition_or.status_string();
    partition_path_ = std::move(partition_or).value();
  }

  void TearDown() override {
    if (ramdisk_ != nullptr) {
      ramdisk_destroy(ramdisk_);
    }
  }

 private:
  ramdisk_client_t* ramdisk_ = nullptr;
  std::string partition_path_;
};  // namespace

using PartitionOverFvmWithRamdiskCase = PartitionOverFvmWithRamdiskFixture;

// Reformat the partition using a number of slices and verify that there are as many slices as
// originally pre-allocated.
TEST_F(PartitionOverFvmWithRamdiskCase, MkfsMinfsWithMinFvmSlices) {
  MkfsOptions options;
  size_t base_slices = 0;
  ASSERT_EQ(Mkfs(partition_path(), kDiskFormatMinfs, launch_stdio_sync, options), ZX_OK);
  fbl::unique_fd partition_fd(open(partition_path(), O_RDONLY));
  ASSERT_TRUE(partition_fd);
  fdio_cpp::UnownedFdioCaller caller(partition_fd.get());
  GetPartitionSliceCount(zx::unowned_channel(caller.borrow_channel()), &base_slices);
  options.fvm_data_slices += 10;

  ASSERT_EQ(Mkfs(partition_path(), kDiskFormatMinfs, launch_stdio_sync, options), ZX_OK);
  size_t allocated_slices = 0;
  GetPartitionSliceCount(zx::unowned_channel(caller.borrow_channel()), &allocated_slices);
  EXPECT_GE(allocated_slices, base_slices + 10);

  DiskFormat actual_format = DetectDiskFormat(partition_fd.get());
  ASSERT_EQ(actual_format, kDiskFormatMinfs);
}

}  // namespace
}  // namespace fs_management
