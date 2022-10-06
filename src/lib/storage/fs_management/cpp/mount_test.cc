// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/storage/fs_management/cpp/mount.h"

#include <errno.h>
#include <fcntl.h>
#include <fidl/fuchsia.fs/cpp/wire.h>
#include <fidl/fuchsia.hardware.block.volume/cpp/wire.h>
#include <fidl/fuchsia.hardware.block/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/vfs.h>
#include <lib/syslog/cpp/macros.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <zircon/device/block.h>
#include <zircon/syscalls.h>

#include <utility>

#include <fbl/unique_fd.h>
#include <gtest/gtest.h>
#include <ramdevice-client/ramdisk.h>

#include "src/lib/fxl/test/test_settings.h"
#include "src/lib/storage/fs_management/cpp/admin.h"
#include "src/lib/storage/fs_management/cpp/fvm.h"
#include "src/storage/fvm/format.h"
#include "src/storage/testing/fvm.h"
#include "src/storage/testing/ram_disk.h"

namespace fs_management {
namespace {

const char* kTestMountPath = "/test/mount";

void CheckMountedFs(const char* path, const char* fs_name) {
  fbl::unique_fd fd(open(path, O_RDONLY | O_DIRECTORY));
  ASSERT_TRUE(fd);

  fdio_cpp::FdioCaller caller(std::move(fd));
  auto result = fidl::WireCall(caller.directory())->QueryFilesystem();
  ASSERT_EQ(result.status(), ZX_OK);
  ASSERT_EQ(result.value().s, ZX_OK);
  fuchsia_io::wire::FilesystemInfo info = *result.value().info;
  ASSERT_EQ(strncmp(fs_name, reinterpret_cast<char*>(info.name.data()), strlen(fs_name)), 0);
  ASSERT_LE(info.used_nodes, info.total_nodes) << "Used nodes greater than free nodes";
  ASSERT_LE(info.used_bytes, info.total_bytes) << "Used bytes greater than free bytes";
  // TODO(planders): eventually check that total/used counts are > 0
}

class RamdiskTestFixture : public testing::Test {
 public:
  explicit RamdiskTestFixture() = default;

  void SetUp() override {
    auto ramdisk_or = storage::RamDisk::Create(512, 1 << 16);
    ASSERT_EQ(ramdisk_or.status_value(), ZX_OK);
    ramdisk_ = std::move(*ramdisk_or);

    ASSERT_EQ(Mkfs(ramdisk_path().c_str(), kDiskFormatMinfs, LaunchStdioSync, MkfsOptions()),
              ZX_OK);
  }

  std::string ramdisk_path() const { return ramdisk_.path(); }
  ramdisk_client_t* ramdisk_client() const { return ramdisk_.client(); }
  fbl::unique_fd ramdisk_fd() const {
    fbl::unique_fd fd(open(ramdisk_path().c_str(), O_RDWR));
    EXPECT_TRUE(fd);
    return fd;
  }

  // Mounts a minfs formatted partition to the desired point.
  zx::status<std::pair<fs_management::StartedSingleVolumeFilesystem, NamespaceBinding>> MountMinfs(
      bool read_only) const {
    MountOptions options;
    options.readonly = read_only;

    auto mounted_filesystem = Mount(ramdisk_fd(), kDiskFormatMinfs, options, LaunchStdioAsync);
    if (mounted_filesystem.is_error())
      return mounted_filesystem.take_error();
    auto data_root = mounted_filesystem->DataRoot();
    if (data_root.is_error())
      return data_root.take_error();
    auto binding = NamespaceBinding::Create(kTestMountPath, *std::move(data_root));
    if (binding.is_error())
      return binding.take_error();
    CheckMountedFs(kTestMountPath, "minfs");
    return zx::ok(std::make_pair(std::move(*mounted_filesystem), std::move(*binding)));
  }

  // Formats the ramdisk with minfs, and writes a small file to it.
  void CreateTestFile(const char* file_name) const {
    auto mounted_filesystem_or = MountMinfs(/*read_only=*/false);
    ASSERT_EQ(mounted_filesystem_or.status_value(), ZX_OK);

    fbl::unique_fd root_fd(open(kTestMountPath, O_RDONLY | O_DIRECTORY));
    ASSERT_TRUE(root_fd);
    fbl::unique_fd fd(openat(root_fd.get(), file_name, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR));
    ASSERT_TRUE(fd);
    ASSERT_EQ(write(fd.get(), "hello", 6), 6);
  }

 private:
  storage::RamDisk ramdisk_;
};

using MountTest = RamdiskTestFixture;

TEST_F(MountTest, MountRemount) {
  // We should still be able to mount and unmount the filesystem multiple times
  for (size_t i = 0; i < 10; i++) {
    auto fs = MountMinfs(/*read_only=*/false);
    ASSERT_EQ(fs.status_value(), ZX_OK);
  }
}

TEST_F(MountTest, MountFsck) {
  {
    auto mounted_filesystem_or = MountMinfs(/*read_only=*/false);
    ASSERT_EQ(mounted_filesystem_or.status_value(), ZX_OK);
  }

  // Fsck shouldn't require any user input for a newly mkfs'd filesystem.
  ASSERT_EQ(Fsck(ramdisk_path().c_str(), kDiskFormatMinfs, FsckOptions(), LaunchStdioSync), ZX_OK);
}

// Tests that setting read-only on the mount options works as expected.
TEST_F(MountTest, MountReadonly) {
  const char file_name[] = "some_file";
  CreateTestFile(file_name);

  bool read_only = true;
  auto mounted_filesystem_or = MountMinfs(read_only);
  ASSERT_EQ(mounted_filesystem_or.status_value(), ZX_OK);

  fbl::unique_fd root_fd(open(kTestMountPath, O_RDONLY | O_DIRECTORY));
  ASSERT_TRUE(root_fd);
  fbl::unique_fd fd(openat(root_fd.get(), file_name, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR));

  // We can no longer open the file as writable
  ASSERT_FALSE(fd);

  // We CAN open it as readable though
  fd.reset(openat(root_fd.get(), file_name, O_RDONLY));
  ASSERT_TRUE(fd);
  ASSERT_LT(write(fd.get(), "hello", 6), 0);
  char buf[6];
  ASSERT_EQ(read(fd.get(), buf, 6), 6);
  ASSERT_EQ(memcmp(buf, "hello", 6), 0);

  ASSERT_LT(renameat(root_fd.get(), file_name, root_fd.get(), "new_file"), 0);
  ASSERT_LT(unlinkat(root_fd.get(), file_name, 0), 0);
}

// Test that when a block device claims to be read-only, the filesystem is mounted as read-only.
TEST_F(MountTest, MountBlockReadonly) {
  const char file_name[] = "some_file";
  CreateTestFile(file_name);

  uint32_t flags = BLOCK_FLAG_READONLY;
  ASSERT_EQ(ramdisk_set_flags(ramdisk_client(), flags), ZX_OK);

  bool read_only = false;
  auto mounted_filesystem_or = MountMinfs(read_only);
  ASSERT_EQ(mounted_filesystem_or.status_value(), ZX_OK);

  // We can't modify the file.
  fbl::unique_fd root_fd(open(kTestMountPath, O_RDONLY | O_DIRECTORY));
  ASSERT_TRUE(root_fd);
  fbl::unique_fd fd(openat(root_fd.get(), file_name, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR));
  ASSERT_FALSE(fd);

  // We can open it as read-only.
  fd.reset(openat(root_fd.get(), file_name, O_RDONLY));
  ASSERT_TRUE(fd);
}

TEST_F(MountTest, StatfsTest) {
  auto mounted_filesystem_or = MountMinfs(/*read_only=*/false);
  ASSERT_EQ(mounted_filesystem_or.status_value(), ZX_OK);

  errno = 0;
  struct statfs stats;
  int rc = statfs("", &stats);
  int err = errno;
  ASSERT_EQ(rc, -1);
  ASSERT_EQ(err, ENOENT);

  rc = statfs(kTestMountPath, &stats);
  ASSERT_EQ(rc, 0);

  // Verify that at least some values make sense, without making the test too brittle.
  ASSERT_EQ(stats.f_type, fidl::ToUnderlying(fuchsia_fs::VfsType::kMinfs));
  ASSERT_NE(stats.f_fsid.__val[0] | stats.f_fsid.__val[1], 0);
  ASSERT_EQ(stats.f_bsize, 8192u);
  ASSERT_EQ(stats.f_namelen, 255u);
  ASSERT_GT(stats.f_bavail, 0u);
  ASSERT_GT(stats.f_ffree, 0u);
}

TEST_F(MountTest, StatvfsTest) {
  auto mounted_filesystem_or = MountMinfs(/*read_only=*/false);
  ASSERT_EQ(mounted_filesystem_or.status_value(), ZX_OK);

  errno = 0;
  struct statvfs stats;
  int rc = statvfs("", &stats);
  int err = errno;
  ASSERT_EQ(rc, -1);
  ASSERT_EQ(err, ENOENT);

  rc = statvfs(kTestMountPath, &stats);
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

void GetPartitionSliceCount(fidl::UnownedClientEnd<fuchsia_hardware_block_volume::Volume> volume,
                            size_t* out_count) {
  auto res = fidl::WireCall(volume)->GetVolumeInfo();
  ASSERT_EQ(res.status(), ZX_OK);
  ASSERT_EQ(res.value().status, ZX_OK);

  size_t allocated_slices = 0;
  std::vector<uint64_t> start_slices = {0};
  while (start_slices[0] < res.value().manager->max_virtual_slice) {
    auto res =
        fidl::WireCall(volume)->QuerySlices(fidl::VectorView<uint64_t>::FromExternal(start_slices));
    ASSERT_EQ(res.status(), ZX_OK);
    ASSERT_EQ(res.value().status, ZX_OK);

    start_slices[0] += res.value().response[0].count;
    if (res.value().response[0].allocated) {
      allocated_slices += res.value().response[0].count;
    }
  }

  // The two methods of getting the partition slice count should agree.
  ASSERT_EQ(res.value().volume->partition_slice_count, allocated_slices);

  *out_count = allocated_slices;
}

class PartitionOverFvmWithRamdiskFixture : public testing::Test {
 public:
  const char* partition_path() const { return partition_path_.c_str(); }

 protected:
  static constexpr uint64_t kBlockSize = 512;

  void SetUp() override {
    size_t ramdisk_block_count = zx_system_get_physmem() / (1024);
    auto ramdisk_or = storage::RamDisk::Create(kBlockSize, ramdisk_block_count);
    ASSERT_EQ(ramdisk_or.status_value(), ZX_OK);
    ramdisk_ = std::move(*ramdisk_or);

    uint64_t slice_size = kBlockSize * (2 << 10);
    auto partition_or =
        storage::CreateFvmPartition(ramdisk_.path(), static_cast<size_t>(slice_size));
    ASSERT_TRUE(partition_or.is_ok()) << partition_or.status_string();
    partition_path_ = std::move(partition_or).value();
  }

 private:
  storage::RamDisk ramdisk_;
  std::string partition_path_;
};

using PartitionOverFvmWithRamdiskCase = PartitionOverFvmWithRamdiskFixture;

// Reformat the partition using a number of slices and verify that there are as many slices as
// originally pre-allocated.
TEST_F(PartitionOverFvmWithRamdiskCase, MkfsMinfsWithMinFvmSlices) {
  MkfsOptions options;
  size_t base_slices = 0;
  ASSERT_EQ(Mkfs(partition_path(), kDiskFormatMinfs, LaunchStdioSync, options), ZX_OK);
  fbl::unique_fd partition_fd(open(partition_path(), O_RDONLY));
  ASSERT_TRUE(partition_fd);
  fdio_cpp::UnownedFdioCaller caller(partition_fd.get());
  GetPartitionSliceCount(caller.borrow_as<fuchsia_hardware_block_volume::Volume>(), &base_slices);
  options.fvm_data_slices += 10;

  ASSERT_EQ(Mkfs(partition_path(), kDiskFormatMinfs, LaunchStdioSync, options), ZX_OK);
  size_t allocated_slices = 0;
  GetPartitionSliceCount(caller.borrow_as<fuchsia_hardware_block_volume::Volume>(),
                         &allocated_slices);
  EXPECT_GE(allocated_slices, base_slices + 10);

  DiskFormat actual_format = DetectDiskFormat(partition_fd.get());
  ASSERT_EQ(actual_format, kDiskFormatMinfs);
}

}  // namespace
}  // namespace fs_management
