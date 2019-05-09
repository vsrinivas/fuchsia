// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_DEVICE_TEST_WITH_DEVICE_H_
#define SRC_VIRTUALIZATION_BIN_VMM_DEVICE_TEST_WITH_DEVICE_H_

#include <fuchsia/virtualization/hardware/cpp/fidl.h>
#include <lib/sys/cpp/testing/test_with_environment.h>

#include "src/virtualization/bin/vmm/device/phys_mem.h"

class TestWithDevice : public sys::testing::TestWithEnvironment {
 protected:
  zx_status_t LaunchDevice(
      const std::string& url, size_t phys_mem_size,
      fuchsia::virtualization::hardware::StartInfo* start_info,
      std::unique_ptr<sys::testing::EnvironmentServices> services = nullptr);
  zx_status_t WaitOnInterrupt();

  std::unique_ptr<sys::testing::EnclosingEnvironment> enclosing_environment_;
  std::shared_ptr<sys::ServiceDirectory> services_;
  fuchsia::sys::ComponentControllerPtr component_controller_;

  zx::event event_;
  PhysMem phys_mem_;
};

#endif  // SRC_VIRTUALIZATION_BIN_VMM_DEVICE_TEST_WITH_DEVICE_H_
