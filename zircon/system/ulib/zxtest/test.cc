// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>

#include <zircon/assert.h>
#include <zxtest/base/test.h>

namespace zxtest {

void Test::Run() {
  ZX_DEBUG_ASSERT_MSG(driver_ != nullptr, "Runner must set the test driver.");
  SetUp();
  // Only execute the test body if there were no set up errors.
  if (driver_->Continue()) {
    TestBody();
  }
  // Even if errors ocurred, we might want to clean any resources.
  TearDown();
}

}  // namespace zxtest
