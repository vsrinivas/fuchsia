// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/controller/realm_utils.h"

#include <lib/syslog/cpp/macros.h>

zx_status_t CreateDynamicComponent(
    fuchsia::component::RealmSyncPtr& realm, const char* collection_name,
    const char* component_name, const char* component_url,
    fit::function<zx_status_t(std::shared_ptr<sys::ServiceDirectory> services)> callback) {
  fuchsia::component::decl::Child child_decl;
  child_decl.set_name(component_name)
      .set_url(component_url)
      .set_startup(fuchsia::component::decl::StartupMode::LAZY)
      .set_on_terminate(fuchsia::component::decl::OnTerminate::NONE);

  fuchsia::component::Realm_CreateChild_Result create_res;
  zx_status_t status = realm->CreateChild({.name = collection_name}, std::move(child_decl),
                                          fuchsia::component::CreateChildArgs(), &create_res);

  if (status != ZX_OK || create_res.is_err()) {
    FX_PLOGS(ERROR, status) << "Failed to CreateDynamicChild. Realm_CreateChild_Result: "
                            << static_cast<long>(create_res.err());
    if (status == ZX_OK) {
      status = ZX_ERR_NOT_FOUND;
    }
    return status;
  }

  fuchsia::component::Realm_OpenExposedDir_Result open_res;
  fidl::InterfaceHandle<fuchsia::io::Directory> exposed_dir;
  status = realm->OpenExposedDir({.name = component_name, .collection = collection_name},
                                 exposed_dir.NewRequest(), &open_res);
  if (status != ZX_OK || open_res.is_err()) {
    FX_PLOGS(ERROR, status)
        << "Failed to OpenExposedDir on dynamic child. Realm_OpenExposedDir_Result: "
        << static_cast<long>(open_res.err());
    if (status == ZX_OK) {
      status = ZX_ERR_NOT_FOUND;
    }
    return status;
  }

  auto child_services = std::make_shared<sys::ServiceDirectory>(std::move(exposed_dir));
  return callback(std::move(child_services));
}
