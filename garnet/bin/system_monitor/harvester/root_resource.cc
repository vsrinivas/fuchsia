// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "root_resource.h"

#include <fcntl.h>
#include <fuchsia/boot/c/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fdio.h>
#include <lib/zx/channel.h>

#include "src/lib/fxl/logging.h"

namespace harvester {

zx_status_t GetRootResource(zx_handle_t* root_resource) {
  zx::channel local, remote;
  zx_status_t status = zx::channel::create(0, &local, &remote);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Cannot create a channel.";
    return status;
  }
  const char* root_resource_svc = "/svc/fuchsia.boot.RootResource";
  status = fdio_service_connect(root_resource_svc, remote.release());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Cannot open fuchsia.boot.RootResource."
                   << zx_status_get_string(status);
    return ZX_ERR_NOT_FOUND;
  }

  zx_status_t fidl_status =
      fuchsia_boot_RootResourceGet(local.get(), root_resource);
  if (fidl_status != ZX_OK) {
    FXL_LOG(ERROR) << "FIDL issue while trying to get root resource: "
                   << zx_status_get_string(fidl_status);
    return fidl_status;
  }
  return ZX_OK;
}

}  // namespace harvester
