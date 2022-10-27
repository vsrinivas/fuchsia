// Copyright 2018 The Fuchsia Authors. All rights reserved.
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
#include <lib/sys/component/cpp/service_client.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <zircon/types.h>

#include <utility>

#include <fbl/string_buffer.h>
#include <fbl/unique_fd.h>
#include <ramdevice-client/ramnand.h>

namespace {

// Waits for |file| to appear in |dir|, and opens it when it does.  Times out if
// the deadline passes.
zx_status_t WaitForFile(const fbl::unique_fd& dir, const char* file, fbl::unique_fd* out) {
  auto watch_func = [](int dirfd, int event, const char* fn, void* cookie) -> zx_status_t {
    auto file = reinterpret_cast<const char*>(cookie);
    if (event != WATCH_EVENT_ADD_FILE) {
      return ZX_OK;
    }
    if (!strcmp(fn, file)) {
      return ZX_ERR_STOP;
    }
    return ZX_OK;
  };

  zx_status_t status =
      fdio_watch_directory(dir.get(), watch_func, ZX_TIME_INFINITE, const_cast<char*>(file));
  if (status != ZX_ERR_STOP) {
    return status;
  }
  out->reset(openat(dir.get(), file, O_RDWR));
  if (!out->is_valid()) {
    return ZX_ERR_IO;
  }
  return ZX_OK;
}

}  // namespace

namespace ramdevice_client {

__EXPORT
zx_status_t RamNand::Create(fuchsia_hardware_nand::wire::RamNandInfo config,
                            std::optional<RamNand>* out) {
  zx::result ctl = component::Connect<fuchsia_hardware_nand::RamNandCtl>(kBasePath);
  if (ctl.is_error()) {
    fprintf(stderr, "could not connect to RamNandCtl: %s\n", ctl.status_string());
    return ctl.status_value();
  }

  const fidl::WireResult result = fidl::WireCall(ctl.value())->CreateDevice(std::move(config));
  if (!result.ok()) {
    fprintf(stderr, "could not create ram_nand device: %s\n", result.status_string());
    return result.status();
  }
  const fidl::WireResponse response = result.value();
  if (zx_status_t status = response.status; status != ZX_OK) {
    fprintf(stderr, "could not create ram_nand device: %s\n", zx_status_get_string(status));
    return status;
  }

  std::string_view name = response.name.get();

  fbl::unique_fd ram_nand_ctl(open(kBasePath, O_RDONLY | O_DIRECTORY));
  if (!ram_nand_ctl) {
    fprintf(stderr, "Could not open ram_nand_ctl");
    return ZX_ERR_INTERNAL;
  }

  fbl::unique_fd fd;
  if (zx_status_t status = WaitForFile(ram_nand_ctl, fbl::String(name).c_str(), &fd);
      status != ZX_OK) {
    fprintf(stderr, "could not open ram_nand: %s\n", zx_status_get_string(status));
    return status;
  }
  fdio_cpp::FdioCaller caller(std::move(fd));
  zx::result ram_nand = caller.take_as<fuchsia_device::Controller>();
  if (ram_nand.is_error()) {
    return ram_nand.status_value();
  }

  *out = RamNand(std::move(ram_nand.value()), fbl::String::Concat({kBasePath, "/", name}), name);

  return ZX_OK;
}

__EXPORT
RamNand::~RamNand() {
  if (unbind) {
    const fidl::WireResult result = fidl::WireCall(controller_)->ScheduleUnbind();
    if (!result.ok()) {
      fprintf(stderr, "Could not unbind ram_nand: %s\n", result.FormatDescription().c_str());
      return;
    }
    const fit::result response = result.value();
    if (response.is_error()) {
      fprintf(stderr, "Could not unbind ram_nand: %s\n",
              zx_status_get_string(response.error_value()));
    }
  }
}

}  // namespace ramdevice_client
