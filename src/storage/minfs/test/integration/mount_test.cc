// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <fcntl.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/fd.h>
#include <lib/fit/defer.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <utility>

#include <fbl/string.h>
#include <fbl/string_buffer.h>
#include <fbl/unique_fd.h>
#include <fs-management/mount.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <ramdevice-client/ramdisk.h>

#include "src/lib/storage/block_client/cpp/block_device.h"
#include "src/lib/storage/block_client/cpp/remote_block_device.h"
#include "src/storage/minfs/format.h"
#include "src/storage/minfs/fsck.h"
#include "src/storage/minfs/minfs.h"
#include "src/storage/testing/ram_disk.h"

namespace minfs {
namespace {

namespace fio = fuchsia_io;

template <bool repairable>
class MountTestTemplate : public testing::Test {
 public:
  void SetUp() final {
    ramdisk_ = storage::RamDisk::Create(/*block_size=*/512, /*block_count=*/1 << 16).value();

    ramdisk_path_ = ramdisk_->path();
    ASSERT_EQ(fs_management::Mkfs(ramdisk_path_.c_str(), fs_management::kDiskFormatMinfs,
                                  launch_stdio_sync, fs_management::MkfsOptions()),
              0);

    int ramdisk_block_fd = ramdisk_get_block_fd(ramdisk_->client());
    zx::channel block_channel;
    ASSERT_EQ(fdio_fd_clone(ramdisk_block_fd, block_channel.reset_and_get_address()), ZX_OK);
    std::unique_ptr<block_client::RemoteBlockDevice> device;
    ASSERT_EQ(block_client::RemoteBlockDevice::Create(std::move(block_channel), &device), ZX_OK);
    auto bcache_or = minfs::CreateBcache(std::move(device));
    ASSERT_TRUE(bcache_or.is_ok());
    ASSERT_FALSE(bcache_or->is_read_only);
    bcache_ = std::move(bcache_or->bcache);

    ASSERT_EQ(zx::channel::create(0, &root_client_end_, &root_server_end_), ZX_OK);
    ASSERT_EQ(loop_.StartThread("minfs test dispatcher"), ZX_OK);
  }

  void ReadSuperblock(minfs::Superblock* out) {
    fbl::unique_fd fd(open(ramdisk_path_.c_str(), O_RDONLY));
    EXPECT_TRUE(fd);

    EXPECT_EQ(pread(fd.get(), out, sizeof(*out), minfs::kSuperblockStart * minfs::kMinfsBlockSize),
              static_cast<ssize_t>(sizeof(*out)));
  }

  void Unmount() {
    if (unmounted_) {
      return;
    }
    // Unmount the filesystem, thereby terminating the minfs instance.
    // TODO(fxbug.dev/34531): After deprecating the DirectoryAdmin interface, switch to unmount
    // using the admin service found within the export directory.
    EXPECT_EQ(
        fidl::WireCall<fuchsia_io_admin::DirectoryAdmin>(zx::unowned_channel(root_client_end()))
            ->Unmount()
            .status(),
        ZX_OK);
    unmounted_ = true;
  }

  void TearDown() final { Unmount(); }

 protected:
  ramdisk_client_t* ramdisk() const { return ramdisk_->client(); }

  const char* ramdisk_path() const { return ramdisk_path_.c_str(); }

  std::unique_ptr<minfs::Bcache> bcache() { return std::move(bcache_); }

  minfs::MountOptions mount_options() const {
    return minfs::MountOptions{.readonly_after_initialization = false,
                               .metrics = false,
                               .verbose = true,
                               .repair_filesystem = repairable,
                               .fvm_data_slices = fs_management::MkfsOptions().fvm_data_slices};
  }

  const zx::channel& root_client_end() { return root_client_end_; }

  zx::channel clone_root_client_end() {
    zx::channel clone_root_client_end, clone_root_server_end;
    ZX_ASSERT(zx::channel::create(0, &clone_root_client_end, &clone_root_server_end) == ZX_OK);
    ZX_ASSERT(fidl::WireCall<fio::Node>(zx::unowned_channel(root_client_end()))
                  ->Clone(fio::wire::kCloneFlagSameRights, std::move(clone_root_server_end))
                  .ok());
    return clone_root_client_end;
  }

  fbl::unique_fd clone_root_as_fd() {
    zx::channel clone_client_end = clone_root_client_end();
    fbl::unique_fd root_fd;
    EXPECT_EQ(fdio_fd_create(clone_client_end.release(), root_fd.reset_and_get_address()), ZX_OK);
    EXPECT_TRUE(root_fd.is_valid());
    return root_fd;
  }

  zx::channel& root_server_end() { return root_server_end_; }

  async::Loop& loop() { return loop_; }

  zx_status_t MountAndServe(minfs::ServeLayout serve_layout) {
    auto fs_or = minfs::MountAndServe(
        mount_options(), loop().dispatcher(), bcache(), std::move(root_server_end()),
        [this]() { loop().Quit(); }, serve_layout);
    if (fs_or.is_error()) {
      return fs_or.error_value();
    }
    fs_ = std::move(fs_or).value();
    return ZX_OK;
  }

 private:
  bool unmounted_ = false;
  std::optional<storage::RamDisk> ramdisk_;
  std::string ramdisk_path_;
  std::unique_ptr<minfs::Bcache> bcache_ = nullptr;
  zx::channel root_client_end_;
  zx::channel root_server_end_;
  std::unique_ptr<fs::ManagedVfs> fs_;
  async::Loop loop_ = async::Loop(&kAsyncLoopConfigNoAttachToCurrentThread);
};

using MountTest = MountTestTemplate<false>;

TEST_F(MountTest, ServeDataRootCheckInode) {
  ASSERT_EQ(MountAndServe(minfs::ServeLayout::kDataRootOnly), ZX_OK);

  // Verify that |root_client_end| corresponds to the root of the filesystem.
  auto attr_result = fidl::WireCall<fio::Node>(zx::unowned_channel(root_client_end()))->GetAttr();
  ASSERT_EQ(attr_result.status(), ZX_OK);
  ASSERT_EQ(attr_result->s, ZX_OK);
  EXPECT_EQ(attr_result->attributes.id, minfs::kMinfsRootIno);
}

TEST_F(MountTest, ServeDataRootAllowFileCreationInRoot) {
  ASSERT_EQ(MountAndServe(minfs::ServeLayout::kDataRootOnly), ZX_OK);

  // Adding a file is allowed here...
  fbl::unique_fd root_fd = clone_root_as_fd();
  ASSERT_TRUE(root_fd.is_valid());
  {
    fbl::unique_fd foo_fd(openat(root_fd.get(), "foo", O_CREAT));
    EXPECT_TRUE(foo_fd.is_valid());
  }
}

TEST_F(MountTest, ServeExportDirectoryExportRootDirectoryEntries) {
  ASSERT_EQ(MountAndServe(minfs::ServeLayout::kExportDirectory), ZX_OK);
  fbl::unique_fd root_fd = clone_root_as_fd();
  ASSERT_TRUE(root_fd.is_valid());

  // Verify that |root_client_end| corresponds to the export directory.
  struct dirent* entry = nullptr;
  fbl::unique_fd dir_fd(dup(root_fd.get()));
  ASSERT_TRUE(dir_fd.is_valid());
  DIR* dir = fdopendir(dir_fd.get());
  ASSERT_NE(dir, nullptr);
  dir_fd.release();
  auto close_dir = fit::defer([&]() { closedir(dir); });

  // Verify that there are exactly two entries, "root" and "diagnostics".
  // TODO(fxbug.dev/34531): Adjust this test accordingly when the admin service is added.
  std::vector<std::string> directory_entries;
  while ((entry = readdir(dir)) != nullptr) {
    if ((strcmp(entry->d_name, ".") != 0) && (strcmp(entry->d_name, "..") != 0)) {
      directory_entries.emplace_back(entry->d_name);
      EXPECT_EQ(entry->d_type, DT_DIR);
    }
  }
  EXPECT_THAT(directory_entries, testing::UnorderedElementsAre("root", "diagnostics"));
}

TEST_F(MountTest, ServeExportDirectoryDisallowFileCreationInExportRoot) {
  ASSERT_EQ(MountAndServe(minfs::ServeLayout::kExportDirectory), ZX_OK);
  fbl::unique_fd root_fd = clone_root_as_fd();
  ASSERT_TRUE(root_fd.is_valid());

  // Adding a file is disallowed here...
  fbl::unique_fd foo_fd(openat(root_fd.get(), "foo", O_CREAT | O_EXCL | O_RDWR));
  EXPECT_FALSE(foo_fd.is_valid());
}

TEST_F(MountTest, ServeExportDirectoryAllowFileCreationInDataRoot) {
  ASSERT_EQ(MountAndServe(minfs::ServeLayout::kExportDirectory), ZX_OK);
  fbl::unique_fd root_fd = clone_root_as_fd();
  ASSERT_TRUE(root_fd.is_valid());

  // Adding a file in "root/" is allowed, since "root/" is within the mutable minfs filesystem.
  fbl::unique_fd foo_fd(openat(root_fd.get(), "root/foo", O_CREAT | O_EXCL | O_RDWR));
  EXPECT_TRUE(foo_fd.is_valid());
}

using RepairableMountTest = MountTestTemplate<true>;

// After successful mount, superblock's clean bit should be cleared and
// persisted to the disk. Reading superblock from raw disk should return cleared
// clean bit.
TEST_F(RepairableMountTest, SyncDuringMount) {
  minfs::Superblock info;
  ReadSuperblock(&info);
  ASSERT_EQ(minfs::kMinfsFlagClean & info.flags, minfs::kMinfsFlagClean);
  ASSERT_EQ(MountAndServe(minfs::ServeLayout::kExportDirectory), ZX_OK);

  // Reading raw device after mount should get us superblock with clean bit
  // unset.
  ReadSuperblock(&info);
  ASSERT_EQ(minfs::kMinfsFlagClean & info.flags, 0u);
}

// After successful unmount, superblock's clean bit should be set and persisted
// to the disk. Reading superblock from raw disk should return set clean bit.
TEST_F(RepairableMountTest, SyncDuringUnmount) {
  minfs::Superblock info;
  ASSERT_EQ(MountAndServe(minfs::ServeLayout::kExportDirectory), ZX_OK);

  // Reading raw device after mount should get us superblock with clean bit
  // unset.
  ReadSuperblock(&info);
  ASSERT_EQ(minfs::kMinfsFlagClean & info.flags, 0u);
  Unmount();

  // Reading raw device after unmount should get us superblock with clean bit
  // set.
  ReadSuperblock(&info);
  ASSERT_EQ(minfs::kMinfsFlagClean & info.flags, minfs::kMinfsFlagClean);
}

}  // namespace
}  // namespace minfs
