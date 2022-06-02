// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fidl/fuchsia.driver.test/cpp/wire.h>
#include <lib/service/llcpp/service.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/syslog/global.h>

#include <sdk/lib/device-watcher/cpp/device-watcher.h>
#include <src/lib/files/file.h>
#include <src/lib/fsl/vmo/file.h>
#include <src/lib/fsl/vmo/sized_vmo.h>
#include <src/lib/storage/block_client/cpp/client.h>
#include <src/lib/storage/block_client/cpp/remote_block_device.h>
#include <src/storage/testing/ram_disk.h>

const int kRamdiskBlockSize = 1024;
constexpr char kExt4FilePath[] = "/pkg/data/factory_ext4.img";

zx::status<storage::RamDisk> MakeRamdisk() {
  fsl::SizedVmo result;
  if (!fsl::VmoFromFilename(kExt4FilePath, &result)) {
    FX_LOGS(ERROR) << "Failed to read file " << kExt4FilePath;
    return zx::make_status(ZX_ERR_INTERNAL).take_error();
  }

  auto size = result.size();
  zx::vmo vmo;
  result.vmo().create_child(ZX_VMO_CHILD_SNAPSHOT_AT_LEAST_ON_WRITE, 0, size, &vmo);

  auto ram_disk_or = storage::RamDisk::CreateWithVmo(std::move(vmo), kRamdiskBlockSize);
  if (!ram_disk_or.is_ok()) {
    FX_LOGS(ERROR) << "Ramdisk failed to be created ";
  } else {
    FX_LOGS(INFO) << "Ramdisk created at " << ram_disk_or.value().path();
  }

  return ram_disk_or;
}

int main() {
  auto client_end = service::Connect<fuchsia_driver_test::Realm>();
  if (!client_end.is_ok()) {
    FX_LOGF(ERROR, "factory_driver_test_realm", "Failed to connect to Realm FIDL: %d",
            client_end.error_value());
    return 1;
  }
  auto client = fidl::BindSyncClient(std::move(*client_end));

  fidl::Arena arena;
  auto wire_result = client->Start(fuchsia_driver_test::wire::RealmArgs::Builder(arena)
                                       .root_driver("fuchsia-boot:///#driver/platform-bus.so")
                                       .Build());
  if (wire_result.status() != ZX_OK) {
    FX_LOGF(ERROR, "factory_driver_test_realm", "Failed to call to Realm:Start: %d",
            wire_result.status());
    return 1;
  }
  if (wire_result->is_error()) {
    FX_LOGF(ERROR, "factory_driver_test_realm", "Realm:Start failed: %d",
            wire_result->error_value());
    return 1;
  }

  fbl::unique_fd out;
  zx_status_t status =
      device_watcher::RecursiveWaitForFile("/dev/sys/platform/00:00:2d/ramctl", &out);
  if (status != ZX_OK) {
    FX_LOGF(ERROR, "factory_driver_test_realm", "Failed to wait for ramctl: %d", status);
  }

  auto result = MakeRamdisk();
  // Keep the ramdisk until the test finishes.
  exit(0);
  return 0;
}
