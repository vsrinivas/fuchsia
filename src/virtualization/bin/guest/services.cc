// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services.h"

#include <fuchsia/virtualization/cpp/fidl.h>
#include <lib/zx/status.h>
#include <zircon/status.h>

#include <iostream>

#include "sdk/lib/sys/cpp/component_context.h"

zx::status<fuchsia::virtualization::ManagerSyncPtr> ConnectToManager(
    sys::ComponentContext* context) {
  fuchsia::virtualization::ManagerSyncPtr manager;
  zx_status_t status = context->svc()->Connect(manager.NewRequest());
  if (status != ZX_OK) {
    std::cerr << "Could not connect to virtualization Manager service: "
              << zx_status_get_string(status) << ".\n";
    return zx::error(status);
  }

  return zx::ok(std::move(manager));
}

zx::status<fuchsia::virtualization::RealmSyncPtr> ConnectToEnvironment(
    sys::ComponentContext* context, uint32_t env_id) {
  // Connect to the manager.
  zx::status<fuchsia::virtualization::ManagerSyncPtr> manager = ConnectToManager(context);
  if (manager.is_error()) {
    return manager.take_error();
  }

  // Connect to the given environment.
  fuchsia::virtualization::RealmSyncPtr realm;
  zx_status_t status = manager->Connect(env_id, realm.NewRequest());
  if (status != ZX_OK) {
    std::cerr << "Could not connect to environment " << env_id << ": "
              << zx_status_get_string(status) << ".\n";
    return zx::error(status);
  }

  return zx::ok(std::move(realm));
}

zx::status<fuchsia::virtualization::GuestSyncPtr> ConnectToGuest(sys::ComponentContext* context,
                                                                 uint32_t env_id, uint32_t cid) {
  zx::status<fuchsia::virtualization::RealmSyncPtr> env_ptr = ConnectToEnvironment(context, env_id);
  if (env_ptr.is_error()) {
    return env_ptr.take_error();
  }

  fuchsia::virtualization::GuestSyncPtr guest;
  zx_status_t status = env_ptr->ConnectToInstance(cid, guest.NewRequest());
  if (status != ZX_OK) {
    std::cerr << "Could not connect to guest instance: " << zx_status_get_string(status) << ".\n";
    return zx::error(status);
  }

  return zx::ok(std::move(guest));
}
