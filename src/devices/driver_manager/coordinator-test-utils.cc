// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "coordinator-test-utils.h"

#include <zxtest/zxtest.h>

CoordinatorConfig DefaultConfig(async_dispatcher_t* dispatcher, devmgr::BootArgs* boot_args) {
  CoordinatorConfig config{};
  const char config1[] = "key1=old-value\0key2=value2\0key1=new-value";
  if (boot_args != nullptr) {
    CreateBootArgs(config1, sizeof(config1), boot_args);
  }
  config.dispatcher = dispatcher;
  config.require_system = false;
  config.asan_drivers = false;
  config.boot_args = boot_args;
  config.fs_provider = new DummyFsProvider();
  config.suspend_fallback = true;
  config.suspend_timeout = zx::sec(2);
  config.resume_timeout = zx::sec(2);
  return config;
}

void CreateBootArgs(const char* config, size_t size, devmgr::BootArgs* boot_args) {
  zx::vmo vmo;
  zx_status_t status = zx::vmo::create(size, 0, &vmo);
  ASSERT_OK(status);

  status = vmo.write(config, 0, size);
  ASSERT_OK(status);

  status = devmgr::BootArgs::Create(std::move(vmo), size, boot_args);
  ASSERT_OK(status);
}

void InitializeCoordinator(Coordinator* coordinator) {
  zx_status_t status = coordinator->InitCoreDevices(kSystemDriverPath);
  ASSERT_OK(status);

  // Load the component driver
  load_driver(kComponentDriverPath, fit::bind_member(coordinator, &Coordinator::DriverAddedInit));

  // Add the driver we're using as platform bus
  load_driver(kSystemDriverPath, fit::bind_member(coordinator, &Coordinator::DriverAddedInit));

  // Initialize devfs.
  devfs_init(coordinator->root_device(), coordinator->dispatcher());
  status = devfs_publish(coordinator->root_device(), coordinator->test_device());
  status = devfs_publish(coordinator->root_device(), coordinator->sys_device());
  ASSERT_OK(status);
  coordinator->set_running(true);
}
