// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fidl/fuchsia.device/cpp/wire.h>
#include <fidl/fuchsia.hardware.nand/cpp/wire.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/unsafe.h>
#include <lib/fdio/watcher.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <zircon/types.h>

#include <utility>

#include <fbl/string_buffer.h>
#include <fbl/unique_fd.h>
#include <ramdevice-client-test/ramnandctl.h>

namespace ramdevice_client_test {

__EXPORT
zx_status_t RamNandCtl::Create(std::unique_ptr<RamNandCtl>* out) {
  driver_integration_test::IsolatedDevmgr::Args args;
  args.disable_block_watcher = true;
  // TODO(surajmalhotra): Remove creation of isolated devmgr from this lib so that caller can choose
  // their creation parameters.
  args.board_name = "astro";

  driver_integration_test::IsolatedDevmgr devmgr;
  if (zx_status_t status = driver_integration_test::IsolatedDevmgr::Create(&args, &devmgr);
      status != ZX_OK) {
    fprintf(stderr, "Could not create ram_nand_ctl device: %s\n", zx_status_get_string(status));
    return status;
  }

  fbl::unique_fd fd;
  if (zx_status_t status = device_watcher::RecursiveWaitForFile(
          devmgr.devfs_root(), "sys/platform/00:00:2e/nand-ctl", &fd);
      status != ZX_OK) {
    fprintf(stderr, "ram_nand_ctl device failed enumerated: %s\n", zx_status_get_string(status));
    return status;
  }
  fdio_cpp::FdioCaller caller(std::move(fd));
  zx::result ctl = caller.take_as<fuchsia_hardware_nand::RamNandCtl>();
  if (ctl.is_error()) {
    fprintf(stderr, "Failed to get ram_nand_ctl device channel: %s\n", ctl.status_string());
    return ctl.status_value();
  }

  *out = std::unique_ptr<RamNandCtl>(new RamNandCtl(std::move(devmgr), std::move(ctl.value())));
  return ZX_OK;
}

__EXPORT
zx_status_t RamNandCtl::CreateRamNand(fuchsia_hardware_nand::wire::RamNandInfo config,
                                      std::optional<ramdevice_client::RamNand>* out) const {
  const fidl::WireResult result = fidl::WireCall(ctl())->CreateDevice(std::move(config));
  if (!result.ok()) {
    fprintf(stderr, "Could not create ram_nand device: %s\n", result.status_string());
    return result.status();
  }
  const fidl::WireResponse response = result.value();
  if (zx_status_t status = response.status; status != ZX_OK) {
    fprintf(stderr, "Could not create ram_nand device: %s\n", zx_status_get_string(status));
    return status;
  }

  fbl::String path = fbl::String::Concat({
      "sys/platform/00:00:2e/nand-ctl/",
      response.name.get(),
  });
  fprintf(stderr, "Trying to open (%s)\n", path.c_str());

  fbl::unique_fd fd;
  if (zx_status_t status = device_watcher::RecursiveWaitForFile(devfs_root(), path.c_str(), &fd);
      status != ZX_OK) {
    return status;
  }
  fdio_cpp::FdioCaller caller(std::move(fd));
  zx::result ram_nand = caller.take_as<fuchsia_device::Controller>();
  if (ram_nand.is_error()) {
    fprintf(stderr, "Failed to get ram_nand device channel: %s\n", ram_nand.status_string());
    return ram_nand.status_value();
  }

  *out = ramdevice_client::RamNand(std::move(ram_nand.value()));
  return ZX_OK;
}

__EXPORT
zx_status_t RamNandCtl::CreateWithRamNand(fuchsia_hardware_nand::wire::RamNandInfo config,
                                          std::optional<ramdevice_client::RamNand>* out) {
  std::unique_ptr<RamNandCtl> ctl;
  if (zx_status_t status = RamNandCtl::Create(&ctl); status != ZX_OK) {
    return status;
  }
  return ctl->CreateRamNand(std::move(config), out);
}

}  // namespace ramdevice_client_test
