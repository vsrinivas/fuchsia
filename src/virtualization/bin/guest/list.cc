// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/guest/list.h"

#include <fuchsia/virtualization/cpp/fidl.h>
#include <zircon/status.h>

#include <iostream>

#include "src/virtualization/bin/guest/services.h"

zx_status_t handle_list(sys::ComponentContext* context) {
  // Connect to the manager.
  zx::status<fuchsia::virtualization::ManagerSyncPtr> manager = ConnectToManager(context);
  if (manager.is_error()) {
    return manager.error_value();
  }

  // Get list of environments.
  std::vector<fuchsia::virtualization::EnvironmentInfo> env_infos;
  zx_status_t status = manager->List(&env_infos);
  if (status != ZX_OK) {
    std::cerr << "Could not fetch list of environments: " << zx_status_get_string(status) << ".\n";
    return status;
  }

  // Print out the environments.
  if (env_infos.empty()) {
    printf("no environments\n");
  } else {
    for (const auto& env_info : env_infos) {
      printf("env:%-4u          %s\n", env_info.id, env_info.label.c_str());
      if (env_info.instances.empty()) {
        printf(" no guest instances\n");
      }
      for (const auto& instance_info : env_info.instances) {
        printf(" guest:%-4u       %s\n", instance_info.cid, instance_info.label.c_str());
      }
    }
  }

  return ZX_OK;
}
