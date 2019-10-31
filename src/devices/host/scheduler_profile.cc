// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "scheduler_profile.h"

#include <fuchsia/scheduler/c/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/zx/channel.h>
#include <lib/zx/profile.h>
#include <stdio.h>
#include <string.h>

namespace devmgr {

zx_handle_t scheduler_profile_provider;

zx_status_t devhost_connect_scheduler_profile_provider() {
  zx::channel registry_client;
  zx::channel registry_service;
  zx_status_t status = zx::channel::create(0u, &registry_client, &registry_service);
  if (status != ZX_OK)
    return status;

  status = fdio_service_connect("/svc/" fuchsia_scheduler_ProfileProvider_Name,
                                registry_service.release());
  if (status != ZX_OK)
    return status;

  scheduler_profile_provider = registry_client.release();
  return ZX_OK;
}

zx_status_t devhost_get_scheduler_profile(uint32_t priority, const char* name,
                                          zx_handle_t* profile) {
  zx_status_t fidl_status = ZX_ERR_INTERNAL;
  zx_status_t status = fuchsia_scheduler_ProfileProviderGetProfile(
      scheduler_profile_provider, priority, name, strlen(name), &fidl_status, profile);
  if (status != ZX_OK) {
    return status;
  }
  if (fidl_status != ZX_OK) {
    return fidl_status;
  }
  return ZX_OK;
}

}  // namespace devmgr
