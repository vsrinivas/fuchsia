// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/blobfs/test/integration/fdio_test.h"

#include <fuchsia/io/llcpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>

#include <blobfs/mkfs.h>

namespace blobfs {

constexpr uint32_t kBlockSize = 512;
constexpr uint32_t kNumBlocks = 8192;

void FdioTest::SetUp() {
  loop_ = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);

  auto device = std::make_unique<block_client::FakeBlockDevice>(kNumBlocks, kBlockSize);
  block_device_ = device.get();
  ASSERT_EQ(FormatFilesystem(block_device_, FilesystemOptions{}), ZX_OK);

  zx::channel root_client, root_server;
  ASSERT_EQ(zx::channel::create(0, &root_client, &root_server), ZX_OK);

  zx::channel diagnostics_dir_server;
  ASSERT_EQ(zx::channel::create(0, &diagnostics_dir_client_, &diagnostics_dir_server), ZX_OK);

  std::unique_ptr<Runner> runner;
  ASSERT_EQ(Runner::Create(loop_.get(), std::move(device), MountOptions(),
                           std::move(vmex_resource_), std::move(diagnostics_dir_server), &runner),
            ZX_OK);
  ASSERT_EQ(runner->ServeRoot(std::move(root_server), layout_), ZX_OK);
  ASSERT_EQ(loop_->StartThread("blobfs test dispatcher"), ZX_OK);

  runner_ = std::move(runner);

  // FDIO serving the root directory.
  ASSERT_EQ(fdio_fd_create(root_client.release(), root_fd_.reset_and_get_address()), ZX_OK);
  ASSERT_TRUE(root_fd_.is_valid());
}

void FdioTest::TearDown() {
  zx::channel root_client;
  ASSERT_EQ(fdio_fd_transfer(root_fd_.release(), root_client.reset_and_get_address()), ZX_OK);
  ASSERT_EQ(
      llcpp::fuchsia::io::DirectoryAdmin::Call::Unmount(zx::unowned_channel(root_client)).status(),
      ZX_OK);
}

}  // namespace blobfs
