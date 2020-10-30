// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "nand_test.h"

#include <fuchsia/device/llcpp/fidl.h>
#include <fuchsia/io/llcpp/fidl.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/namespace.h>
#include <lib/zx/vmar.h>
#include <zircon/rights.h>

#include <optional>
#include <string>

#include <blobfs/mkfs.h>
#include <block-client/cpp/remote-block-device.h>

namespace blobfs {

namespace {

constexpr uint32_t kPageSize = 4096;
constexpr uint32_t kOobSize = 8;
constexpr uint32_t kPagesPerBlock = 64;
constexpr uint32_t kNumBlocks = 20;

fuchsia_hardware_nand_RamNandInfo GetRamNandConfig() {
  fuchsia_hardware_nand_RamNandInfo config = {};
  config.nand_info.page_size = kPageSize;
  config.nand_info.pages_per_block = kPagesPerBlock;
  config.nand_info.num_blocks = kNumBlocks;
  config.nand_info.ecc_bits = 8;
  config.nand_info.oob_size = kOobSize;
  config.nand_info.nand_class = fuchsia_hardware_nand_Class_FTL;

  return config;
}

// Returns the topological path of a device represented by a file descriptor.
std::optional<std::string> GetTopologicalPath(int fd) {
  fdio_cpp::UnownedFdioCaller device(fd);

  auto resp = llcpp::fuchsia::device::Controller::Call::GetTopologicalPath(device.channel());
  if (resp.status() != ZX_OK || resp->result.is_err()) {
    return std::nullopt;
  }

  auto& r = resp->result.response();
  size_t dev_prefix_length = strlen("/dev");
  return std::string(r.path.data() + dev_prefix_length, r.path.size() - dev_prefix_length);
}

}  // namespace

NandTest::Connection::Connection(const char* dev_root, zx::vmo vmo, bool create_filesystem) {
  std::string thread_name = std::string("blobfs dispatcher for ") + std::string(dev_root);
  loop_ = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
  loop_->StartThread(thread_name.c_str());

  auto ram_nand_config = GetRamNandConfig();
  ASSERT_OK(ramdevice_client::RamNandCtl::Create(&ram_nand_ctl_));

  fdio_ns_t* name_space = nullptr;
  ASSERT_EQ(ZX_OK, fdio_ns_get_installed(&name_space));
  ASSERT_EQ(ZX_OK, fdio_ns_bind_fd(name_space, dev_root, ram_nand_ctl_->devfs_root().get()));

  if (vmo) {
    zx_handle_duplicate(vmo.get(), ZX_RIGHT_SAME_RIGHTS, &ram_nand_config.vmo);
  }

  ASSERT_EQ(ZX_OK, ramdevice_client::RamNand::Create(ram_nand_ctl_, &ram_nand_config, &ram_nand_));
  ASSERT_TRUE(ram_nand_.has_value());
  auto ram_nand_topo_path = GetTopologicalPath(ram_nand_->fd().get());
  ASSERT_TRUE(ram_nand_topo_path.has_value() && !ram_nand_topo_path->empty());
  // Get the FTL device path which is bound under the nand device.
  std::string ftl = std::string(dev_root) + *ram_nand_topo_path + "/ftl";
  std::string block_device_path = ftl + "/block";
  int block_fd = open(block_device_path.c_str(), O_RDWR);
  ASSERT_LE(0, block_fd);

  // Connect a block device client to the device.
  zx::channel block_channel;
  ASSERT_OK(fdio_get_service_handle(block_fd, block_channel.reset_and_get_address()));
  std::unique_ptr<block_client::RemoteBlockDevice> device;
  ASSERT_OK(block_client::RemoteBlockDevice::Create(std::move(block_channel), &device));
  block_device_ = device.get();

  if (create_filesystem) {
    // Create an empty blobfs on the block device.
    ASSERT_OK(blobfs::FormatFilesystem(device.get(), FilesystemOptions{}));
  }

  zx::channel root_client, root_server;
  ASSERT_OK(zx::channel::create(0, &root_client, &root_server));

  zx::channel diagnostics_dir_client, diagnostics_dir_server;
  ASSERT_OK(zx::channel::create(0, &diagnostics_dir_client, &diagnostics_dir_server));

  // Create the blobfs. This takes ownership of the device.
  ASSERT_OK(blobfs::Runner::Create(loop_.get(), std::move(device), MountOptions(), zx::resource(),
                                   std::move(diagnostics_dir_server), &runner_));
  ASSERT_OK(runner_->ServeRoot(std::move(root_server), blobfs::ServeLayout::kDataRootOnly));

  // FDIO serving the root directory.
  ASSERT_OK(fdio_fd_create(root_client.release(), root_fd_.reset_and_get_address()));
  ASSERT_TRUE(root_fd_.is_valid());
}

NandTest::Connection::~Connection() {
  zx::channel root_client;
  ASSERT_OK(fdio_fd_transfer(root_fd_.release(), root_client.reset_and_get_address()));
  ASSERT_OK(
      llcpp::fuchsia::io::DirectoryAdmin::Call::Unmount(zx::unowned_channel(root_client)).status());
}

size_t NandTest::Connection::GetVMOSize() {
  return (kPageSize + kOobSize) * (kPagesPerBlock * kNumBlocks);
}

void NandTest::SetUp() {}

}  // namespace blobfs
