// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "coordinator_test_utils.h"

#include <fidl/fuchsia.boot/cpp/wire.h>

#include <mock-boot-arguments/server.h>
#include <zxtest/zxtest.h>

CoordinatorConfig DefaultConfig(async_dispatcher_t* bootargs_dispatcher,
                                mock_boot_arguments::Server* boot_args,
                                fidl::WireSyncClient<fuchsia_boot::Arguments>* client) {
  // The DummyFsProvider is stateless.  Create a single static one here so that we don't need to
  // manage pointer lifetime for it below.
  static DummyFsProvider dummy_fs_provider;

  CoordinatorConfig config;

  if (boot_args != nullptr && client != nullptr) {
    *boot_args = mock_boot_arguments::Server{{{"key1", "new-value"}, {"key2", "value2"}}};
    boot_args->CreateClient(bootargs_dispatcher, client);
  }
  config.require_system = false;
  config.boot_args = client;
  config.fs_provider = &dummy_fs_provider;
  config.suspend_timeout = zx::sec(2);
  config.resume_timeout = zx::sec(2);
  config.path_prefix = "/pkg/";
  // Should be MEXEC to verify the test behavior without rebooting.
  config.default_shutdown_system_state = statecontrol_fidl::wire::SystemPowerState::kMexec;
  return config;
}

void InitializeCoordinator(Coordinator* coordinator) {
  zx_status_t status = coordinator->InitCoreDevices(kSystemDriverPath);
  ASSERT_OK(status);

  // Add the driver we're using as platform bus
  load_driver(nullptr, kSystemDriverPath,
              fit::bind_member(coordinator, &Coordinator::DriverAddedInit));

  // Initialize devfs.
  devfs_init(coordinator->root_device(), coordinator->dispatcher());
  status = devfs_publish(coordinator->root_device(), coordinator->sys_device());
  ASSERT_OK(status);
  devfs_connect_diagnostics(coordinator->inspect_manager().diagnostics_client());
  coordinator->set_running(true);
}
