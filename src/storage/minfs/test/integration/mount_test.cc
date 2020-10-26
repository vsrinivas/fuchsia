// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <fcntl.h>
#include <fuchsia/io/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/fd.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <utility>

#include <block-client/cpp/block-device.h>
#include <block-client/cpp/remote-block-device.h>
#include <fbl/auto_call.h>
#include <fbl/string.h>
#include <fbl/string_buffer.h>
#include <fbl/unique_fd.h>
#include <fs-management/mount.h>
#include <fs-test-utils/fixture.h>
#include <fs/test_support/environment.h>
#include <ramdevice-client/ramdisk.h>
#include <zxtest/zxtest.h>

#include "src/storage/minfs/format.h"
#include "src/storage/minfs/fsck.h"
#include "src/storage/minfs/minfs.h"

namespace {

namespace fio = ::llcpp::fuchsia::io;

template <bool repairable>
class MountTestTemplate : public zxtest::Test {
 public:
  void SetUp() final {
    ASSERT_EQ(ramdisk_create_at(fs::g_environment->devfs_root().get(), 512, 1 << 16, &ramdisk_),
              ZX_OK);
    ramdisk_path_ = std::string("/fake/dev/") + ramdisk_get_path(ramdisk_);
    ASSERT_OK(
        mkfs(ramdisk_path_.c_str(), DISK_FORMAT_MINFS, launch_stdio_sync, &default_mkfs_options));

    int ramdisk_block_fd = ramdisk_get_block_fd(ramdisk_);
    zx::channel block_channel;
    ASSERT_OK(fdio_fd_clone(ramdisk_block_fd, block_channel.reset_and_get_address()));
    std::unique_ptr<block_client::RemoteBlockDevice> device;
    ASSERT_OK(block_client::RemoteBlockDevice::Create(std::move(block_channel), &device));
    bool readonly_device = false;
    ASSERT_OK(minfs::CreateBcache(std::move(device), &readonly_device, &bcache_));
    ASSERT_FALSE(readonly_device);

    ASSERT_OK(zx::channel::create(0, &root_client_end_, &root_server_end_));
    ASSERT_OK(loop_.StartThread("minfs test dispatcher"));
  }

  void ReadSuperblock(minfs::Superblock* out) {
    fbl::unique_fd fd(open(ramdisk_path_.c_str(), O_RDONLY));
    EXPECT_TRUE(fd);

    EXPECT_EQ(pread(fd.get(), out, sizeof(*out), minfs::kSuperblockStart * minfs::kMinfsBlockSize),
              sizeof(*out));
  }

  void Unmount() {
    if (unmounted_) {
      return;
    }
    // Unmount the filesystem, thereby terminating the minfs instance.
    // TODO(fxbug.dev/34531): After deprecating the DirectoryAdmin interface, switch to unmount
    // using the admin service found within the export directory.
    EXPECT_OK(fio::DirectoryAdmin::Call::Unmount(zx::unowned_channel(root_client_end())).status());
    unmounted_ = true;
  }

  void TearDown() final {
    Unmount();
    ASSERT_OK(ramdisk_destroy(ramdisk_));
  }

 protected:
  ramdisk_client_t* ramdisk() const { return ramdisk_; }

  const char* ramdisk_path() const { return ramdisk_path_.c_str(); }

  std::unique_ptr<minfs::Bcache> bcache() { return std::move(bcache_); }

  minfs::MountOptions mount_options() const {
    return minfs::MountOptions{.readonly_after_initialization = false,
                               .metrics = false,
                               .verbose = true,
                               .repair_filesystem = repairable,
                               .fvm_data_slices = default_mkfs_options.fvm_data_slices};
  }

  const zx::channel& root_client_end() { return root_client_end_; }

  zx::channel clone_root_client_end() {
    zx::channel clone_root_client_end, clone_root_server_end;
    ZX_ASSERT(zx::channel::create(0, &clone_root_client_end, &clone_root_server_end) == ZX_OK);
    ZX_ASSERT(fio::Node::Call::Clone(zx::unowned_channel(root_client_end()),
                                     fio::CLONE_FLAG_SAME_RIGHTS, std::move(clone_root_server_end))
                  .ok());
    return clone_root_client_end;
  }

  fbl::unique_fd clone_root_as_fd() {
    zx::channel clone_client_end = clone_root_client_end();
    fbl::unique_fd root_fd;
    EXPECT_OK(fdio_fd_create(clone_client_end.release(), root_fd.reset_and_get_address()));
    EXPECT_TRUE(root_fd.is_valid());
    return root_fd;
  }

  zx::channel& root_server_end() { return root_server_end_; }

  async::Loop& loop() { return loop_; }

  zx_status_t MountAndServe(minfs::ServeLayout serve_layout) {
    return minfs::MountAndServe(
        mount_options(), loop().dispatcher(), bcache(), std::move(root_server_end()),
        [this]() { loop().Quit(); }, serve_layout);
  }

 private:
  bool unmounted_ = false;
  ramdisk_client_t* ramdisk_ = nullptr;
  std::string ramdisk_path_;
  std::unique_ptr<minfs::Bcache> bcache_ = nullptr;
  zx::channel root_client_end_ = {};
  zx::channel root_server_end_ = {};
  async::Loop loop_ = async::Loop(&kAsyncLoopConfigNoAttachToCurrentThread);
};

using MountTest = MountTestTemplate<false>;

TEST_F(MountTest, ServeDataRootCheckInode) {
  ASSERT_OK(MountAndServe(minfs::ServeLayout::kDataRootOnly));

  // Verify that |root_client_end| corresponds to the root of the filesystem.
  auto attr_result = fio::Node::Call::GetAttr(zx::unowned_channel(root_client_end()));
  ASSERT_OK(attr_result.status());
  ASSERT_OK(attr_result->s);
  EXPECT_EQ(attr_result->attributes.id, minfs::kMinfsRootIno);
}

TEST_F(MountTest, ServeDataRootAllowFileCreationInRoot) {
  ASSERT_OK(MountAndServe(minfs::ServeLayout::kDataRootOnly));

  // Adding a file is allowed here...
  fbl::unique_fd root_fd = clone_root_as_fd();
  ASSERT_TRUE(root_fd.is_valid());
  {
    fbl::unique_fd foo_fd(openat(root_fd.get(), "foo", O_CREAT));
    EXPECT_TRUE(foo_fd.is_valid());
  }
}

TEST_F(MountTest, ServeExportDirectoryExportRootDirectoryEntries) {
  ASSERT_OK(MountAndServe(minfs::ServeLayout::kExportDirectory));
  fbl::unique_fd root_fd = clone_root_as_fd();
  ASSERT_TRUE(root_fd.is_valid());

  // Verify that |root_client_end| corresponds to the export directory.
  struct dirent* entry = nullptr;
  fbl::unique_fd dir_fd(dup(root_fd.get()));
  ASSERT_TRUE(dir_fd.is_valid());
  DIR* dir = fdopendir(dir_fd.get());
  ASSERT_NOT_NULL(dir);
  dir_fd.release();
  fbl::AutoCall close_dir([&]() { closedir(dir); });
  int count = 0;
  // Verify that there is exactly one entry called "root".
  // TODO(fxbug.dev/34531): Adjust this test accordingly when the admin service is added.
  while ((entry = readdir(dir)) != nullptr) {
    if ((strcmp(entry->d_name, ".") != 0) && (strcmp(entry->d_name, "..") != 0)) {
      EXPECT_STR_EQ(entry->d_name, "root");
      EXPECT_EQ(entry->d_type, DT_DIR);
      EXPECT_EQ(count, 0);
      count++;
    }
  }
  EXPECT_EQ(count, 1);
}

TEST_F(MountTest, ServeExportDirectoryDisallowFileCreationInExportRoot) {
  ASSERT_OK(MountAndServe(minfs::ServeLayout::kExportDirectory));
  fbl::unique_fd root_fd = clone_root_as_fd();
  ASSERT_TRUE(root_fd.is_valid());

  // Adding a file is disallowed here...
  fbl::unique_fd foo_fd(openat(root_fd.get(), "foo", O_CREAT | O_EXCL | O_RDWR));
  EXPECT_FALSE(foo_fd.is_valid());
}

TEST_F(MountTest, ServeExportDirectoryAllowFileCreationInDataRoot) {
  ASSERT_OK(MountAndServe(minfs::ServeLayout::kExportDirectory));
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
  ASSERT_OK(MountAndServe(minfs::ServeLayout::kExportDirectory));

  // Reading raw device after mount should get us superblock with clean bit
  // unset.
  ReadSuperblock(&info);
  ASSERT_EQ(minfs::kMinfsFlagClean & info.flags, 0);
}

// After successful unmount, superblock's clean bit should be set and persisted
// to the disk. Reading superblock from raw disk should return set clean bit.
TEST_F(RepairableMountTest, SyncDuringUnmount) {
  minfs::Superblock info;
  ASSERT_OK(MountAndServe(minfs::ServeLayout::kExportDirectory));

  // Reading raw device after mount should get us superblock with clean bit
  // unset.
  ReadSuperblock(&info);
  ASSERT_EQ(minfs::kMinfsFlagClean & info.flags, 0);
  Unmount();

  // Reading raw device after unmount should get us superblock with clean bit
  // set.
  ReadSuperblock(&info);
  ASSERT_EQ(minfs::kMinfsFlagClean & info.flags, minfs::kMinfsFlagClean);
}

}  // namespace
