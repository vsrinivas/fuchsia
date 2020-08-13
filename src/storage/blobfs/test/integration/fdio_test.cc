// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fdio_test.h"

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
  ASSERT_OK(FormatFilesystem(block_device_));

  zx::channel root_client, root_server;
  ASSERT_OK(zx::channel::create(0, &root_client, &root_server));

  zx::channel diagnostics_dir_server;
  ASSERT_OK(zx::channel::create(0, &diagnostics_dir_client_, &diagnostics_dir_server));

  blobfs::MountOptions options;
  options.pager = true;
  std::unique_ptr<blobfs::Runner> runner;
  ASSERT_OK(blobfs::Runner::Create(loop_.get(), std::move(device), &options,
                                   std::move(vmex_resource_), std::move(diagnostics_dir_server),
                                   &runner));
  ASSERT_OK(runner->ServeRoot(std::move(root_server), layout_));
  ASSERT_OK(loop_->StartThread("blobfs test dispatcher"));

  runner_ = std::move(runner);

  // FDIO serving the root directory.
  ASSERT_OK(fdio_fd_create(root_client.release(), root_fd_.reset_and_get_address()));
  ASSERT_TRUE(root_fd_.is_valid());
}

void FdioTest::TearDown() {
  zx::channel root_client;
  ASSERT_OK(fdio_fd_transfer(root_fd_.release(), root_client.reset_and_get_address()));
  ASSERT_OK(
      llcpp::fuchsia::io::DirectoryAdmin::Call::Unmount(zx::unowned_channel(root_client)).status());
}

}  // namespace blobfs
