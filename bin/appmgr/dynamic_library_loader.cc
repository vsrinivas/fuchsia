// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/dynamic_library_loader.h"

#include <lib/async-loop/loop.h>
#include <loader-service/loader-service.h>

namespace component {
namespace DynamicLibraryLoader {

static async_loop_t* ld_loop = nullptr;

zx_status_t Start(fxl::UniqueFD fd, zx::channel* result) {
  zx_status_t status = ZX_OK;

  if (!ld_loop) {
    status = async_loop_create(&kAsyncLoopConfigNoAttachToThread, &ld_loop);
    if (status != ZX_OK)
      return status;

    status = async_loop_start_thread(ld_loop, "appmgr-loader", nullptr);
    if (status != ZX_OK)
      return status;
  }

  loader_service_t* svc = nullptr;
  status = loader_service_create_fd(async_loop_get_dispatcher(ld_loop),
                                    fd.release(), -1, &svc);
  if (status != ZX_OK)
    return status;
  status = loader_service_connect(svc, result->reset_and_get_address());
  loader_service_release(svc);
  return status;
}

}  // namespace DynamicLibraryLoader
}  // namespace component
