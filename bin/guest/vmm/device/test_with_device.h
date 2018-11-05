// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_GUEST_VMM_DEVICE_TEST_WITH_DEVICE_H_
#define GARNET_BIN_GUEST_VMM_DEVICE_TEST_WITH_DEVICE_H_

#include <fuchsia/guest/device/cpp/fidl.h>
#include <lib/component/cpp/testing/test_with_environment.h>

#include "garnet/lib/machina/device/phys_mem.h"

class TestWithDevice : public component::testing::TestWithEnvironment {
 protected:
  zx_status_t LaunchDevice(
      const std::string& url, size_t phys_mem_size,
      fuchsia::guest::device::StartInfo* start_info,
      std::unique_ptr<component::testing::EnvironmentServices> services =
          nullptr);
  zx_status_t WaitOnInterrupt();

  std::unique_ptr<component::testing::EnclosingEnvironment>
      enclosing_environment_;
  component::Services services;
  fuchsia::sys::ComponentControllerPtr component_controller_;

  zx::event event_;
  machina::PhysMem phys_mem_;
};

#endif  // GARNET_BIN_GUEST_VMM_DEVICE_TEST_WITH_DEVICE_H_
