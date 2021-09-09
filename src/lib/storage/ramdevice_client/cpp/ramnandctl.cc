// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fidl/fuchsia.device/cpp/wire.h>
#include <fuchsia/device/c/fidl.h>
#include <fuchsia/hardware/nand/c/fidl.h>
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
zx_status_t RamNandCtl::Create(fbl::RefPtr<RamNandCtl>* out) {
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
  st = devmgr_integration_test::RecursiveWaitForFile(devmgr.devfs_root(),
                                                     "sys/platform/00:00:2e/nand-ctl", &ctl);
  if (st != ZX_OK) {
    fprintf(stderr, "ram_nand_ctl device failed enumerated, %d\n", st);
    return st;
  }

  *out = fbl::AdoptRef(new RamNandCtl(std::move(devmgr), std::move(ctl)));
  return ZX_OK;
}

__EXPORT
zx_status_t RamNandCtl::CreateRamNand(const fuchsia_hardware_nand_RamNandInfo* config,
                                      std::optional<RamNand>* out) {
  fdio_t* io = fdio_unsafe_fd_to_io(fd().get());
  if (io == NULL) {
    fprintf(stderr, "Could not get fdio object\n");
    return ZX_ERR_INTERNAL;
  }
  zx_handle_t ctl_svc = fdio_unsafe_borrow_channel(io);

  char name[fuchsia_hardware_nand_NAME_LEN + 1];
  size_t out_name_size;
  zx_status_t status;
  zx_status_t st = fuchsia_hardware_nand_RamNandCtlCreateDevice(
      ctl_svc, config, &status, name, fuchsia_hardware_nand_NAME_LEN, &out_name_size);
  fdio_unsafe_release(io);
  if (st != ZX_OK || status != ZX_OK) {
    st = st != ZX_OK ? st : status;
    fprintf(stderr, "Could not create ram_nand device, %d\n", st);
    return st;
  }
  name[out_name_size] = '\0';

  // TODO(fxbug.dev/33003): We should be able to open relative to ctl->fd(), but
  // due to a bug, we have to be relative to devfs_root instead.
  fbl::StringBuffer<PATH_MAX> path;
  path.Append("sys/platform/00:00:2e/nand-ctl/");
  path.Append(name);
  fprintf(stderr, "Trying to open (%s)\n", path.c_str());

  fbl::unique_fd fd;
  st = devmgr_integration_test::RecursiveWaitForFile(devfs_root(), path.c_str(), &fd);
  if (st != ZX_OK) {
    return st;
  }

  *out = RamNand(std::move(fd));
  return ZX_OK;
}

__EXPORT
zx_status_t RamNandCtl::CreateWithRamNand(const fuchsia_hardware_nand_RamNandInfo* config,
                                          std::optional<RamNand>* out) {
  fbl::RefPtr<RamNandCtl> ctl;
  zx_status_t st = RamNandCtl::Create(&ctl);
  if (st != ZX_OK) {
    return st;
  }
  return ctl->CreateRamNand(config, out);
}

}  // namespace ramdevice_client_test
