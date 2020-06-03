// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dwc2.h"

#include <lib/fake_ddk/fake_ddk.h>
#include <zxtest/zxtest.h>

namespace dwc2 {

TEST(dwc2Test, DdkLifecycle) {
  fake_ddk::Bind ddk;

  zx::interrupt irq;
  ASSERT_OK(zx::interrupt::create(zx::resource(), 0, ZX_INTERRUPT_VIRTUAL, &irq));

  auto dev = std::make_unique<Dwc2>(fake_ddk::kFakeParent);
  dev->SetInterrupt(std::move(irq));
  // This will call the device init hook, which spawns the irq thread.
  ASSERT_OK(dev->DdkAdd("dwc2"));
  dev->DdkAsyncRemove();
  ASSERT_TRUE(ddk.Ok());
}

}  // namespace dwc2Test
