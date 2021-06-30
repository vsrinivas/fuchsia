// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/bin/vulkan_loader/magma_dependency_injection.h"

#include <fuchsia/gpu/magma/cpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/io.h>
#include <lib/fdio/unsafe.h>

zx_status_t MagmaDependencyInjection::Initialize() {
  gpu_dependency_injection_watcher_ = fsl::DeviceWatcher::Create(
      "/dev/class/gpu-dependency-injection", [this](int dir_fd, const std::string filename) {
        if (filename == ".") {
          return;
        }
        fuchsia::gpu::magma::DependencyInjectionSyncPtr dependency_injection;
        zx_status_t status;
        fdio_t* dir_fdio = fdio_unsafe_fd_to_io(dir_fd);
        if (!dir_fdio) {
          FX_LOGS(ERROR) << "Failed to get fdio_t";
          return;
        }
        zx_handle_t dir_handle;
        dir_handle = fdio_unsafe_borrow_channel(dir_fdio);
        if (!dir_handle) {
          FX_LOGS(ERROR) << "Failed to borrow channel";
          return;
        }
        status = fdio_service_connect_at(dir_handle, filename.c_str(),
                                         dependency_injection.NewRequest().TakeChannel().release());
        if (status != ZX_OK) {
          FX_LOGS(ERROR) << "Failed to connect to " << filename;
          return;
        }
        fdio_unsafe_release(dir_fdio);

        dependency_injection->SetMemoryPressureProvider(
            context_->svc()->Connect<fuchsia::memorypressure::Provider>());
      });
  if (!gpu_dependency_injection_watcher_)
    return ZX_ERR_INTERNAL;
  return ZX_OK;
}
