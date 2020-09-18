// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dsi-dw.h"

#include <lib/fake_ddk/fake_ddk.h>
#include <zircon/types.h>

#include <memory>

#include <zxtest/zxtest.h>

#include "zxtest/base/test.h"

namespace dsi_dw {

TEST(DsiDwTest, DdkLifeCycle) {
  fake_ddk::Bind ddk;
  auto dev = std::make_unique<DsiDw>(fake_ddk::kFakeParent);
  EXPECT_OK(dev->DdkAdd("dw-dsi"));
  dev->DdkAsyncRemove();
  EXPECT_TRUE(ddk.Ok());
  dev->DdkRelease();
  __UNUSED auto ptr = dev.release();
}

}  // namespace dsi_dw
