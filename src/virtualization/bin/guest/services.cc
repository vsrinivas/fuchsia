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
