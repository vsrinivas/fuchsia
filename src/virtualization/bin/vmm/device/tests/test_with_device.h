// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_DEVICE_TESTS_TEST_WITH_DEVICE_H_
#define SRC_VIRTUALIZATION_BIN_VMM_DEVICE_TESTS_TEST_WITH_DEVICE_H_

#include <fuchsia/virtualization/hardware/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>

#include "src/lib/testing/loop_fixture/real_loop_fixture.h"
#include "src/virtualization/bin/vmm/device/phys_mem.h"

class TestWithDevice : public gtest::RealLoopFixture {
 protected:
  zx_status_t WaitOnInterrupt();
  zx_status_t MakeStartInfo(size_t phys_mem_size,
                            fuchsia::virtualization::hardware::StartInfo* start_info);

  zx::event event_;
  PhysMem phys_mem_;
};

#endif  // SRC_VIRTUALIZATION_BIN_VMM_DEVICE_TESTS_TEST_WITH_DEVICE_H_
