// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/block/bin/ftl_proxy/ftl_util.h"

#include <fuchsia/hardware/nand/c/fidl.h>
#include <lib/devmgr-integration-test/fixture.h>
#include <lib/devmgr-launcher/launch.h>
#include <lib/fdio/namespace.h>
#include <lib/sync/completion.h>
#include <lib/zx/time.h>
#include <zircon/errors.h>

#include <memory>
#include <string_view>
#include <thread>

#include <fbl/ref_ptr.h>
#include <gtest/gtest.h>
#include <ramdevice-client/ramdisk.h>
#include <ramdevice-client/ramnand.h>

#include "fbl/auto_call.h"

namespace ftl_proxy {
namespace {

fuchsia_hardware_nand_RamNandInfo GetConfig() {
  fuchsia_hardware_nand_RamNandInfo config = {};
  config.nand_info.page_size = 4096;
  config.nand_info.pages_per_block = 64;
  config.nand_info.num_blocks = 20;
  config.nand_info.ecc_bits = 8;
  config.nand_info.oob_size = 8;
  config.nand_info.nand_class = fuchsia_hardware_nand_Class_FTL;
  return config;
}

// The fixture provides an IsolatedDevMgr, an async_loop, and the ability to
// mount a RamNandDevice an a FTL on top.
class FtlUtilTest : public ::testing::Test {
 public:
  static constexpr std::string_view FakeDevFsPath() { return "/fake/dev"; }
  static constexpr std::string_view FakeBlockClassPath() { return "/fake/dev/class/block"; }

  void SetUp() override { CreateIsolatedEnvironment(); }

  void TearDown() override {
    fdio_ns_t* name_space;
    ASSERT_EQ(ZX_OK, fdio_ns_get_installed(&name_space));
    ASSERT_EQ(ZX_OK, fdio_ns_unbind(name_space, FakeDevFsPath().data()));
  }

  void AddRamNandAndFtl() {
    auto config = GetConfig();
    std::optional<ramdevice_client::RamNand> ram_nand;
    ASSERT_EQ(ZX_OK, ramdevice_client::RamNand::Create(ram_nand_ctl_, &config, &ram_nand));
    ram_nands_.push_back(std::move(ram_nand.value()));
  }

  int root() const { return ram_nand_ctl_->devfs_root().get(); }

 private:
  void CreateIsolatedEnvironment() {
    ASSERT_EQ(ZX_OK, ramdevice_client::RamNandCtl::Create(&ram_nand_ctl_));

    fdio_ns_t* name_space;
    ASSERT_EQ(ZX_OK, fdio_ns_get_installed(&name_space));
    ASSERT_EQ(ZX_OK, fdio_ns_bind_fd(name_space, FakeDevFsPath().data(),
                                     ram_nand_ctl_->devfs_root().get()));
  }

  fbl::RefPtr<ramdevice_client::RamNandCtl> ram_nand_ctl_;
  std::vector<ramdevice_client::RamNand> ram_nands_;
};

TEST_F(FtlUtilTest, GetFtlTopologicalPathReturnsWhenDeviceShowsUp) {
  sync_completion_t before = {};
  sync_completion_t after = {};
  std::string topo_result;

  std::thread worker([&]() {
    sync_completion_signal(&before);
    topo_result = GetFtlTopologicalPath(FakeBlockClassPath());
    sync_completion_signal(&after);
  });
  auto worker_cleanup = fbl::MakeAutoCall([&] { worker.join(); });

  sync_completion_wait(&before, zx::time::infinite().get());
  ASSERT_NO_FATAL_FAILURE(AddRamNandAndFtl());
  sync_completion_wait(&after, zx::time::infinite().get());
  size_t ftl_pos = topo_result.rfind("/ftl");
  ASSERT_EQ(topo_result.size() - strlen("/ftl"), ftl_pos);
}

TEST_F(FtlUtilTest, GetFtlTopologicalPathWithDeadlineReturnsIfNoFtl) {
  sync_completion_t after = {};
  std::string topo_result;

  std::thread worker([&]() {
    topo_result = GetFtlTopologicalPath(FakeBlockClassPath(), zx::usec(2));
    sync_completion_signal(&after);
  });
  auto worker_cleanup = fbl::MakeAutoCall([&] { worker.join(); });

  sync_completion_wait(&after, zx::time::infinite().get());
  ASSERT_TRUE(topo_result.empty());
}

TEST_F(FtlUtilTest, GetFtlTopologicalPathIgnoresNonFtlDevices) {
  sync_completion_t before = {};
  sync_completion_t after = {};
  std::string topo_result;
  std::vector<ramdisk_client_t*> clients;
  auto clean_up = fbl::MakeAutoCall([&]() {
    for (auto* client : clients) {
      EXPECT_EQ(ZX_OK, ramdisk_destroy(client));
    }
  });

  std::thread worker([&]() {
    sync_completion_signal(&before);
    topo_result = GetFtlTopologicalPath(FakeBlockClassPath());
    sync_completion_signal(&after);
  });
  auto worker_cleanup = fbl::MakeAutoCall([&] { worker.join(); });

  ASSERT_EQ(ZX_OK, wait_for_device_at(root(), "misc/ramctl", zx::duration::infinite().get()));
  for (uint64_t i = 0; i < 20; ++i) {
    ramdisk_client_t* ramdisk = nullptr;
    ASSERT_EQ(ZX_OK, ramdisk_create_at(root(), 512, 20, &ramdisk));
    clients.push_back(ramdisk);
  }

  sync_completion_wait(&before, zx::time::infinite().get());

  ASSERT_NO_FATAL_FAILURE(AddRamNandAndFtl());
  sync_completion_wait(&after, zx::time::infinite().get());
  size_t ftl_pos = topo_result.rfind("/ftl");
  ASSERT_EQ(topo_result.size() - strlen("/ftl"), ftl_pos);
}

TEST_F(FtlUtilTest, GetFtlInspectVmoReturnsTheVmo) {
  ASSERT_NO_FATAL_FAILURE(AddRamNandAndFtl());
  auto ftl_path = GetFtlTopologicalPath(FakeBlockClassPath());
  auto vmo = GetFtlInspectVmo(ftl_path);

  ASSERT_TRUE(vmo.is_valid());
}

TEST_F(FtlUtilTest, GetDeviceWearCountReturnsTheWearCount) {
  ASSERT_NO_FATAL_FAILURE(AddRamNandAndFtl());
  auto ftl_path = GetFtlTopologicalPath(FakeBlockClassPath());
  auto vmo = GetFtlInspectVmo(ftl_path);

  ASSERT_TRUE(vmo.is_valid());
  ASSERT_TRUE(GetDeviceWearCount(vmo).has_value());
}

}  // namespace
}  // namespace ftl_proxy
