// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "isolated_devmgr.h"

#include <fcntl.h>
#include <lib/async/default.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/unsafe.h>
#include <lib/fzl/fdio.h>
#include <src/lib/fxl/logging.h>
#include <zircon/status.h>

namespace isolated_devmgr {

zx_status_t IsolatedDevmgr::WaitForFile(const char* path) {
  fbl::unique_fd out;
  return devmgr_integration_test::RecursiveWaitForFile(devmgr_.devfs_root(),
                                                       path, &out);
}

void IsolatedDevmgr::Connect(zx::channel req) {
  fzl::UnownedFdioCaller fd(devmgr_.devfs_root().get());
  fdio_service_clone_to(fd.borrow_channel(), req.release());
}

void IsolatedDevmgr::DevmgrException(async_dispatcher_t* dispatcher,
                                     async::ExceptionBase* exception,
                                     zx_status_t status,
                                     const zx_port_packet_t* report) {
  exception->Unbind();

  if (exception_callback_) {
    exception_callback_();
  }
}

std::unique_ptr<IsolatedDevmgr> IsolatedDevmgr::Create(
    devmgr_launcher::Args args, async_dispatcher_t* dispatcher) {
  if (dispatcher == nullptr) {
    dispatcher = async_get_default_dispatcher();
  }
  devmgr_integration_test::IsolatedDevmgr devmgr;

  auto status =
      devmgr_integration_test::IsolatedDevmgr::Create(std::move(args), &devmgr);
  if (status == ZX_OK) {
    return std::make_unique<IsolatedDevmgr>(dispatcher, std::move(devmgr));
  } else {
    FXL_LOG(ERROR) << "Failed to create devmgr: "
                   << zx_status_get_string(status);
    return nullptr;
  }
}

}  // namespace isolated_devmgr
