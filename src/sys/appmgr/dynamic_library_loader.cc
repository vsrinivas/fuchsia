// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/appmgr/dynamic_library_loader.h"

#include <fuchsia/io/llcpp/fidl.h>
#include <lib/async-loop/default.h>
#include <lib/async-loop/loop.h>
#include <lib/fdio/directory.h>
#include <lib/syslog/cpp/macros.h>

#include "src/lib/loader_service/loader_service.h"

namespace component {
namespace DynamicLibraryLoader {

namespace fio = ::llcpp::fuchsia::io;

static async_loop_t* ld_loop = nullptr;

zx::status<zx::channel> Start(int package_fd, std::string name) {
  if (!ld_loop) {
    zx_status_t status = async_loop_create(&kAsyncLoopConfigNoAttachToCurrentThread, &ld_loop);
    if (status != ZX_OK) {
      return zx::error(status);
    }

    status = async_loop_start_thread(ld_loop, "appmgr-loader", nullptr);
    if (status != ZX_OK) {
      return zx::error(status);
    }
  }

  fbl::unique_fd lib_fd;
  zx_status_t status = fdio_open_fd_at(
      package_fd, "lib",
      fio::OPEN_FLAG_DIRECTORY | fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_EXECUTABLE,
      lib_fd.reset_and_get_address());
  if (status != ZX_OK) {
    return zx::error(status);
  }

  auto loader =
      loader::LoaderService::Create(async_loop_get_dispatcher(ld_loop), std::move(lib_fd), name);
  return loader->Connect();
}

}  // namespace DynamicLibraryLoader
}  // namespace component
