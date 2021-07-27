// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dsi-dw.h"

#include <zircon/types.h>

#include <memory>

#include <zxtest/zxtest.h>

#include "src/devices/testing/mock-ddk/mock-device.h"
#include "zxtest/base/test.h"

namespace dsi_dw {

TEST(DsiDwTest, DdkLifeCycle) {
  std::shared_ptr<MockDevice> fake_parent = MockDevice::FakeRootParent();
  auto dev = std::make_unique<DsiDw>(fake_parent.get());
  EXPECT_OK(dev->DdkAdd("dw-dsi"));
  dev.release();
  // TODO(fxbug.dev/79639): Removed the obsolete fake_ddk.Ok() check.
  // To test Unbind and Release behavior, call UnbindOp and ReleaseOp directly.
}

}  // namespace dsi_dw
