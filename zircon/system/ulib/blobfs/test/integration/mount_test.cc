// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/io/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/fd.h>
#include <unistd.h>

#include <blobfs/mount.h>
#include <block-client/cpp/block-device.h>
#include <block-client/cpp/remote-block-device.h>
#include <fbl/auto_call.h>
#include <fbl/unique_fd.h>
#include <fs-management/mount.h>
#include <fs-test-utils/blobfs/blobfs.h>
#include <ramdevice-client/ramdisk.h>
#include <zxtest/zxtest.h>

#include "blobfs.h"
#include "runner.h"

namespace {

namespace fio = ::llcpp::fuchsia::io;

class MountTest : public zxtest::Test {
 public:
  explicit MountTest(blobfs::ServeLayout layout) : layout_(layout) {}

  void SetUp() final {
    ASSERT_OK(ramdisk_create(512, 1 << 16, &ramdisk_));
    ASSERT_OK(mkfs(ramdisk_get_path(ramdisk_), DISK_FORMAT_BLOBFS, launch_stdio_sync,
                   &default_mkfs_options));

    fbl::unique_fd block_fd(ramdisk_get_block_fd(ramdisk_));
    zx::channel block_channel;
    ASSERT_OK(fdio_fd_clone(block_fd.get(), block_channel.reset_and_get_address()));
    std::unique_ptr<block_client::RemoteBlockDevice> device;
    ASSERT_OK(block_client::RemoteBlockDevice::Create(std::move(block_channel), &device));

    blobfs::MountOptions options;
    zx::channel root_client, root_server;
    ASSERT_OK(zx::channel::create(0, &root_client, &root_server));

    loop_ = new async::Loop(&kAsyncLoopConfigNoAttachToCurrentThread);
    std::unique_ptr<blobfs::Runner> runner;

    ASSERT_OK(blobfs::Runner::Create(loop_, std::move(device), &options, &runner));
    ASSERT_OK(runner->ServeRoot(std::move(root_server), layout_));
    ASSERT_OK(loop_->StartThread("blobfs test dispatcher"));
    runner_ = runner.release();

    ASSERT_OK(fdio_fd_create(root_client.release(), root_fd_.reset_and_get_address()));
    ASSERT_TRUE(root_fd_.is_valid());
  }

  void TearDown() final {
    zx::channel root_client;
    ASSERT_OK(fdio_fd_transfer(root_fd_.release(), root_client.reset_and_get_address()));
    ASSERT_OK(fio::DirectoryAdmin::Call::Unmount(zx::unowned_channel(root_client)).status());
    loop_->Shutdown();
    ASSERT_OK(ramdisk_destroy(ramdisk_));
  }

 protected:
  int root_fd() const { return root_fd_.get(); }

 private:
  ramdisk_client_t* ramdisk_ = nullptr;
  async::Loop* loop_ = nullptr;
  blobfs::Runner* runner_ = nullptr;
  blobfs::ServeLayout layout_;
  fbl::unique_fd root_fd_;
};

class DataMountTest : public MountTest {
 public:
  DataMountTest() : MountTest(blobfs::ServeLayout::kDataRootOnly) {}
};

class OutgoingMountTest : public MountTest {
 public:
  OutgoingMountTest() : MountTest(blobfs::ServeLayout::kExportDirectory) {}
};

// merkle root for a file containing the string "test content". in order to create a file on blobfs
// we need the filename to be a valid merkle root whether or not we ever write the content.
constexpr std::string_view kFileName =
    "be901a14ec42ee0a8ee220eb119294cdd40d26d573139ee3d51e4430e7d08c28";

TEST_F(DataMountTest, DataRootHasNoRootDirectoryInIt) {
  errno = 0;
  fbl::unique_fd no_fd(openat(root_fd(), blobfs::kOutgoingDataRoot, O_RDONLY));
  ASSERT_FALSE(no_fd.is_valid());
  ASSERT_EQ(errno, EINVAL);
}

TEST_F(DataMountTest, DataRootCanHaveBlobsCreated) {
  fbl::unique_fd foo_fd(openat(root_fd(), kFileName.data(), O_CREAT));
  ASSERT_TRUE(foo_fd.is_valid());
}

TEST_F(OutgoingMountTest, OutgoingDirectoryHasRootDirectoryInIt) {
  fbl::unique_fd no_fd(openat(root_fd(), blobfs::kOutgoingDataRoot, O_DIRECTORY));
  ASSERT_TRUE(no_fd.is_valid());
}

TEST_F(OutgoingMountTest, OutgoingDirectoryIsReadOnly) {
  fbl::unique_fd foo_fd(openat(root_fd(), kFileName.data(), O_CREAT));
  ASSERT_FALSE(foo_fd.is_valid());
}

TEST_F(OutgoingMountTest, OutgoingDirectoryDataRootCanHaveBlobsCreated) {
  std::string path = std::string("root/") + kFileName.data();
  fbl::unique_fd foo_fd(openat(root_fd(), path.c_str(), O_CREAT));
  ASSERT_TRUE(foo_fd.is_valid());
}

}  // namespace
