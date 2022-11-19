// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_MAIN_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_MAIN_H_

#include "coordinator.h"

// These are helpers for getting sets of parameters over FIDL
struct DriverManagerParams {
  bool require_system;
  bool suspend_timeout_fallback;
  bool verbose;
  DriverHostCrashPolicy crash_policy;
  std::string root_driver;
  bool use_dfv2;
};

// Get the root job from the root job service.
zx::result<zx::job> get_root_job();

// Get the root resource from the root resource service. Not receiving the
// startup handle is logged, but not fatal.  In test environments, it would not
// be present.
zx::result<zx::resource> get_root_resource();

// Get the mexec resource from the mexec resource service. Not receiving the
// startup handle is logged, but not fatal.  In test environments, it would not
// be present.
zx::result<zx::resource> get_mexec_resource();

int RunDfv1(DriverManagerParams driver_manager_params,
            fidl::WireSyncClient<fuchsia_boot::Arguments> boot_args);

int RunDfv2(DriverManagerParams driver_manager_params,
            fidl::WireSyncClient<fuchsia_boot::Arguments> boot_args);

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_MAIN_H_
