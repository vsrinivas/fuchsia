// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fidl/fuchsia.driver.test/cpp/wire.h>
#include <fuchsia/hardware/nand/c/fidl.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/service/llcpp/service.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#include <utility>

#include <fbl/string.h>
#include <fbl/unique_fd.h>
#include <ramdevice-client/ramnand.h>
#include <sdk/lib/device-watcher/cpp/device-watcher.h>
#include <zxtest/zxtest.h>

namespace {

fuchsia_hardware_nand_RamNandInfo BuildConfig() {
  fuchsia_hardware_nand_RamNandInfo config = {};
  config.vmo = ZX_HANDLE_INVALID;
  config.nand_info = {4096, 4, 5, 6, 0, fuchsia_hardware_nand_Class_TEST, {}};
  return config;
}

class NandDevice {
 public:
  explicit NandDevice(const fuchsia_hardware_nand_RamNandInfo& config = BuildConfig()) {
    if (ramdevice_client::RamNand::Create(&config, &ram_nand_) == ZX_OK) {
      // caller_ want's to own the device, so we re-open it even though
      // ram_nand_ already has it open.
      fbl::unique_fd device(dup(ram_nand_->fd().get()));
      caller_.reset(std::move(device));
    }
  }

  ~NandDevice() = default;

  bool IsValid() const { return caller_ ? true : false; }

  const char* path() { return ram_nand_->path(); }
  const char* filename() { return ram_nand_->filename(); }

 private:
  std::optional<ramdevice_client::RamNand> ram_nand_;
  fdio_cpp::FdioCaller caller_;
  DISALLOW_COPY_ASSIGN_AND_MOVE(NandDevice);
};

TEST(RamNandCtlTest, TrivialLifetime) {
  std::unique_ptr<device_watcher::DirWatcher> watcher;
  fbl::unique_fd dir_fd(open(ramdevice_client::RamNand::kBasePath, O_RDONLY | O_DIRECTORY));
  ASSERT_TRUE(dir_fd);
  ASSERT_EQ(device_watcher::DirWatcher::Create(std::move(dir_fd), &watcher), ZX_OK);

  fbl::String path;
  fbl::String filename;
  {
    NandDevice device;
    ASSERT_TRUE(device.IsValid());
    path = fbl::String(device.path());
    filename = fbl::String(device.filename());
  }
  ASSERT_EQ(watcher->WaitForRemoval(filename, zx::sec(5)), ZX_OK);

  fbl::unique_fd found(open(path.c_str(), O_RDWR));
  ASSERT_FALSE(found);
}

TEST(RamNandCtlTest, ExportConfig) {
  fuchsia_hardware_nand_RamNandInfo config = BuildConfig();
  config.export_nand_config = true;

  NandDevice device(config);
  ASSERT_TRUE(device.IsValid());
}

TEST(RamNandCtlTest, ExportPartitions) {
  fuchsia_hardware_nand_RamNandInfo config = BuildConfig();
  config.export_partition_map = true;

  NandDevice device(config);
  ASSERT_TRUE(device.IsValid());
}

TEST(RamNandCtlTest, CreateFailure) {
  fuchsia_hardware_nand_RamNandInfo config = BuildConfig();
  config.nand_info.num_blocks = 0;

  NandDevice device(config);
  ASSERT_FALSE(device.IsValid());
}

}  // namespace

int main(int argc, char** argv) {
  // Connect to DriverTestRealm.
  auto client_end = service::Connect<fuchsia_driver_test::Realm>();
  if (!client_end.is_ok()) {
    fprintf(stderr, "Failed to connect to Realm FIDL: %d", client_end.error_value());
    return 1;
  }
  auto client = fidl::BindSyncClient(std::move(*client_end));

  // Start the DriverTestRealm with correct arguments.
  fidl::Arena arena;
  fuchsia_driver_test::wire::RealmArgs realm_args(arena);
  realm_args.set_root_driver(arena, "fuchsia-boot:///#driver/platform-bus.so");
  auto wire_result = client->Start(realm_args);
  if (wire_result.status() != ZX_OK) {
    fprintf(stderr, "Failed to call to Realm:Start: %d", wire_result.status());
    return 1;
  }
  if (wire_result->is_error()) {
    fprintf(stderr, "Realm:Start failed: %d", wire_result->error_value());
    return 1;
  }
  fbl::unique_fd dir_fd;
  device_watcher::RecursiveWaitForFile(ramdevice_client::RamNand::kBasePath, &dir_fd);
  setlinebuf(stdout);
  return RUN_ALL_TESTS(argc, argv);
}
