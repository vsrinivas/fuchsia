// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "coordinator_test_utils.h"

#include <fuchsia/boot/llcpp/fidl.h>

#include <mock-boot-arguments/server.h>
#include <zxtest/zxtest.h>

CoordinatorConfig DefaultConfig(async_dispatcher_t* dispatcher,
                                async_dispatcher_t* bootargs_dispatcher,
                                mock_boot_arguments::Server* boot_args,
                                llcpp::fuchsia::boot::Arguments::SyncClient* client) {
  CoordinatorConfig config{};
  if (boot_args != nullptr && client != nullptr) {
    *boot_args = mock_boot_arguments::Server{{{"key1", "new-value"}, {"key2", "value2"}}};
    boot_args->CreateClient(bootargs_dispatcher, client);
  }
  config.dispatcher = dispatcher;
  config.require_system = false;
  config.asan_drivers = false;
  config.boot_args = client;
  config.fs_provider = new DummyFsProvider();
  config.suspend_fallback = true;
  config.suspend_timeout = zx::sec(2);
  config.resume_timeout = zx::sec(2);
  return config;
}

void InitializeCoordinator(Coordinator* coordinator) {
  zx_status_t status = coordinator->InitCoreDevices(kSystemDriverPath);
  ASSERT_OK(status);

  // Load the fragment driver
  load_driver(kFragmentDriverPath, fit::bind_member(coordinator, &Coordinator::DriverAddedInit));

  // Add the driver we're using as platform bus
  load_driver(kSystemDriverPath, fit::bind_member(coordinator, &Coordinator::DriverAddedInit));

  // Initialize devfs.
  devfs_init(coordinator->root_device(), coordinator->dispatcher());
  status = devfs_publish(coordinator->root_device(), coordinator->test_device());
  ASSERT_OK(status);
  status = devfs_publish(coordinator->root_device(), coordinator->sys_device());
  ASSERT_OK(status);
  devfs_connect_diagnostics(coordinator->inspect_manager().diagnostics_channel());
  coordinator->set_running(true);
}
