// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crg-udc.h"

#include <zxtest/zxtest.h>

#include "src/devices/testing/mock-ddk/mock-device.h"

namespace crg_udc {

TEST(UdcTest, DdkLifecycle) {
  std::shared_ptr<MockDevice> fake_parent = MockDevice::FakeRootParent();
  zx::interrupt irq;
  ASSERT_OK(zx::interrupt::create(zx::resource(), 0, ZX_INTERRUPT_VIRTUAL, &irq));

  auto dev = std::make_unique<CrgUdc>(fake_parent.get(), std::move(irq));
  // This will call the device init hook, which spawns the irq thread.
  ASSERT_OK(dev->DdkAdd("udc"));
  // Release dev so it can be deleted on release().
  dev.release();
  // TODO(fxbug.dev/79639): Removed the obsolete fake_ddk.Ok() check.
  // To test Unbind and Release behavior, call UnbindOp and ReleaseOp directly.
}

}  // namespace crg_udc
