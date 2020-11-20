// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/device/manager/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/io.h>
#include <lib/fidl/llcpp/server.h>
#include <lib/zbitl/error_stdio.h>
#include <lib/zbitl/image.h>
#include <lib/zbitl/view.h>
#include <lib/zbitl/vmo.h>
#include <lib/zx/vmo.h>
#include <stdio.h>
#include <zircon/processargs.h>
#include <zircon/status.h>

#include <cstddef>
#include <string_view>

#include <fbl/unique_fd.h>

#include "src/bringup/lib/mexec/mexec.h"

constexpr const char* kMexecZbi = "/boot/testdata/mexec-child.zbi";

namespace devmgr = llcpp::fuchsia::device::manager;

namespace {

// No userspace drivers are actually running at the time that this program
// runs as we were launched instead of component_manager; accordingly, fake out
// device suspension, as mexec::Boot expects a service to do so.
struct FakeDeviceAdmin : public devmgr::Administrator::Interface {
  void Suspend(uint32_t flags, SuspendCompleter::Sync& completer) override {
    completer.Reply(flags == devmgr::SUSPEND_FLAG_MEXEC ? ZX_OK : ZX_ERR_INVALID_ARGS);
  }
};

}  // namespace

int main() {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  if (zx_status_t status = loop.StartThread(); status != ZX_OK) {
    printf("failed to start message loop: %s\n", zx_status_get_string(status));
    return ZX_ERR_INTERNAL;
  }
  zx::channel devmgr_channel, remote;
  if (zx_status_t status = zx::channel::create(0, &devmgr_channel, &remote); status != ZX_OK) {
    printf("failed to create a channel: %s\n", zx_status_get_string(status));
    return ZX_ERR_INTERNAL;
  }
  FakeDeviceAdmin admin;
  if (auto result = fidl::BindServer(loop.dispatcher(), std::move(remote), &admin);
      result.is_error()) {
    printf("failed to start server implementing %s: %s\n", devmgr::Administrator::Name,
           zx_status_get_string(result.error()));
    return ZX_ERR_INTERNAL;
  }

  fbl::unique_fd fd{open(kMexecZbi, O_RDONLY)};
  if (!fd) {
    printf("failed to open %s: %s\n", kMexecZbi, strerror(errno));
    return ZX_ERR_INTERNAL;
  }

  zx::vmo kernel_zbi, data_zbi;
  {
    zx::vmo vmo;
    if (zx_status_t status = fdio_get_vmo_exact(fd.get(), vmo.reset_and_get_address());
        status != ZX_OK) {
      printf("failed get child ZBI's VMO: %s\n", zx_status_get_string(status));
      return status;
    }
    zbitl::View view(std::move(vmo));

    if (auto result = view.Copy(view.begin(), ++view.begin()); result.is_error()) {
      zbitl::PrintViewCopyError(result.error_value());
      view.ignore_error();
      return ZX_ERR_INTERNAL;
    } else {
      kernel_zbi = std::move(result).value();
    }

    if (auto result = view.Copy(++view.begin(), view.end()); result.is_error()) {
      zbitl::PrintViewCopyError(result.error_value());
      view.ignore_error();
      return ZX_ERR_INTERNAL;
    } else {
      data_zbi = std::move(result).value();
    }

    if (auto result = view.take_error(); result.is_error()) {
      zbitl::PrintViewError(result.error_value());
      return ZX_ERR_INTERNAL;
    }
  }

  zx::resource root_resource{zx_take_startup_handle(PA_HND(PA_RESOURCE, 0))};
  if (!root_resource.is_valid()) {
    printf("unable to get a hold of the root resource\n");
    return ZX_ERR_INTERNAL;
  }

  if (zx_status_t status = mexec::Boot(std::move(root_resource), std::move(devmgr_channel),
                                       std::move(kernel_zbi), std::move(data_zbi));
      status != ZX_OK) {
    printf("failed to mexec: %s\n", zx_status_get_string(status));
    return status;
  }
  return ZX_OK;
}
