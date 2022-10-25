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
  zx_status_t st = driver_integration_test::IsolatedDevmgr::Create(&args, &devmgr);
  if (st != ZX_OK) {
    fprintf(stderr, "Could not create ram_nand_ctl device, %d\n", st);
    return st;
  }

  fbl::unique_fd ctl;
  st = device_watcher::RecursiveWaitForFile(devmgr.devfs_root(), "sys/platform/00:00:2e/nand-ctl",
                                            &ctl);
  if (st != ZX_OK) {
    fprintf(stderr, "ram_nand_ctl device failed enumerated, %d\n", st);
    return st;
  }

  *out = std::unique_ptr<RamNandCtl>(new RamNandCtl(std::move(devmgr), std::move(ctl)));
  return ZX_OK;
}

__EXPORT
zx_status_t RamNandCtl::CreateRamNand(fuchsia_hardware_nand::wire::RamNandInfo config,
                                      std::optional<RamNand>* out) {
  fdio_cpp::UnownedFdioCaller caller(fd());
  const fidl::WireResult result =
      fidl::WireCall(caller.borrow_as<fuchsia_hardware_nand::RamNandCtl>())
          ->CreateDevice(std::move(config));
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

  *out = RamNand(std::move(fd));
  return ZX_OK;
}

__EXPORT
zx_status_t RamNandCtl::CreateWithRamNand(fuchsia_hardware_nand::wire::RamNandInfo config,
                                          std::optional<RamNand>* out) {
  std::unique_ptr<RamNandCtl> ctl;
  if (zx_status_t status = RamNandCtl::Create(&ctl); status != ZX_OK) {
    return status;
  }
  return ctl->CreateRamNand(std::move(config), out);
}

}  // namespace ramdevice_client_test
