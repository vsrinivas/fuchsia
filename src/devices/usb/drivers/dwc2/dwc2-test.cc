// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dwc2.h"

#include <zxtest/zxtest.h>

#include "src/devices/testing/mock-ddk/mock-device.h"

namespace dwc2 {

TEST(dwc2Test, DdkLifecycle) {
  std::shared_ptr<MockDevice> fake_parent = MockDevice::FakeRootParent();
  zx::interrupt irq;
  ASSERT_OK(zx::interrupt::create(zx::resource(), 0, ZX_INTERRUPT_VIRTUAL, &irq));

  auto dev = std::make_unique<Dwc2>(fake_parent.get());
  dev->SetInterrupt(std::move(irq));
  // This will call the device init hook, which spawns the irq thread.
  ASSERT_OK(dev->DdkAdd("dwc2"));
  // Release dev so it can be deleted on release().
  dev.release();
  // TODO(fxbug.dev/79639): Removed the obsolete fake_ddk.Ok() check.
  // To test Unbind and Release behavior, call UnbindOp and ReleaseOp directly.
}

}  // namespace dwc2
