// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "info_resource.h"

#include <fcntl.h>
#include <fuchsia/kernel/cpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fdio.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/channel.h>

namespace harvester {

zx_status_t GetInfoResource(zx_handle_t* info_resource_handle) {
  zx::channel local, remote;
  zx_status_t status = zx::channel::create(0, &local, &remote);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Cannot create a channel.";
    return status;
  }

  static const std::string kVmexResourcePath =
      "/svc/" + std::string(fuchsia::kernel::InfoResource::Name_);
  status = fdio_service_connect(kVmexResourcePath.c_str(), remote.release());
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Cannot open fuchsia.kernel.InfoResource."
                   << zx_status_get_string(status);
    return ZX_ERR_NOT_FOUND;
  }

  fuchsia::kernel::InfoResource_SyncProxy proxy(std::move(local));
  zx::resource info_resource;
  status = proxy.Get(&info_resource);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "FIDL issue while trying to get info resource: "
                   << zx_status_get_string(status);
    return status;
  }
  *info_resource_handle = info_resource.release();
  return ZX_OK;
}

}  // namespace harvester
