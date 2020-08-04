// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/vmo.h>

#include <ddk/binding.h>
#include <zxtest/zxtest.h>

#include "coordinator.h"
#include "coordinator_test_utils.h"
#include "driver.h"
#include "multiple_device_test.h"

class AutobindTest : public MultipleDeviceTestCase {
  void SetUp() {
    ASSERT_NO_FATAL_FAILURES(MultipleDeviceTestCase::SetUp());

    auto bind_program = std::make_unique<zx_bind_inst_t[]>(1);
    bind_program[0] = BI_MATCH();

    auto drv = std::make_unique<Driver>();
    drv->name = "always_match";
    drv->binding = std::move(bind_program);
    drv->binding_size = sizeof(zx_bind_inst_t);
    drv->libname = "<always_match.so>";
    // Borrow a DSO VMO from another driver, because we need an executable VMO (or else duplicating
    // it to send to the driver host will fail)
    fprintf(stderr, "libname: %s", coordinator()->fragment_driver()->libname.c_str());
    ASSERT_OK(
        coordinator()->LibnameToVmo(coordinator()->fragment_driver()->libname, &drv->dso_vmo));

    coordinator()->DriverAdded(drv.release(), "0.1");
    coordinator_loop()->RunUntilIdle();
    coordinator()->root_device()->proxy()->DetachFromParent();
  }
};

TEST_F(AutobindTest, SkipAutobindFlag) {
  size_t device_index;
  ASSERT_NO_FATAL_FAILURES(
      AddDeviceSkipAutobind(platform_bus(), "skip_autobind", ZX_PROTOCOL_PCI, &device_index));

  coordinator_loop()->RunUntilIdle();
  // If autobind erroneously ran, we'd have a pending message for telling the driver host
  ASSERT_FALSE(DeviceHasPendingMessages(device_index));
}

TEST_F(AutobindTest, NoSkipAutobindFlag) {
  size_t device_index;
  ASSERT_NO_FATAL_FAILURES(AddDevice(platform_bus(), "no_skip_autobind", ZX_PROTOCOL_PCI,
                                     /* driver */ "", &device_index));

  coordinator_loop()->RunUntilIdle();
  ASSERT_TRUE(DeviceHasPendingMessages(device_index));
}
