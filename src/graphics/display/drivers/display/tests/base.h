// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_DISPLAY_TESTS_BASE_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_DISPLAY_TESTS_BASE_H_

#include <fuchsia/hardware/display/llcpp/fidl.h>
#include <lib/async-loop/default.h>
#include <lib/zx/bti.h>

#include <map>
#include <vector>

#include <ddk/platform-defs.h>
#include <ddk/protocol/composite.h>
#include <ddk/protocol/platform/device.h>
#include <ddk/protocol/sysmem.h>
#include <ddktl/device.h>
#include <ddktl/protocol/composite.h>
#include <ddktl/protocol/platform/bus.h>
#include <ddktl/protocol/platform/device.h>
#include <ddktl/protocol/sysmem.h>
#include <fbl/array.h>
#include <zxtest/zxtest.h>

#include "src/graphics/display/drivers/fake/fake-display-device-tree.h"

namespace display {

class TestBase : public zxtest::Test {
 public:
  TestBase() : loop_(&kAsyncLoopConfigAttachToCurrentThread) {}

  void SetUp() override;
  void TearDown() override;

  Binder& ddk() { return tree_->ddk(); }
  Controller* controller() { return tree_->controller(); }
  fake_display::FakeDisplay* display() { return tree_->display(); }
  zx::unowned_channel sysmem_fidl();
  zx::unowned_channel display_fidl();

  async_dispatcher_t* dispatcher() { return loop_.dispatcher(); }
  bool RunLoopWithTimeoutOrUntil(fit::function<bool()>&& condition,
                                 zx::duration timeout = zx::sec(1),
                                 zx::duration step = zx::msec(10));

 private:
  async::Loop loop_;
  thrd_t loop_thrd_ = 0;

  std::unique_ptr<FakeDisplayDeviceTree> tree_;
};

}  // namespace display

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_DISPLAY_TESTS_BASE_H_
