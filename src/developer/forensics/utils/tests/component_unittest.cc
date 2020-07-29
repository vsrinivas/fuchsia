// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/utils/component/component.h"

#include <gtest/gtest.h>

namespace forensics {
namespace component {
namespace {

TEST(ComponentTest, LogPreviousStarts) {
  {
    Component instance1;
    EXPECT_TRUE(instance1.IsFirstInstance());
  }
  {
    Component instance2;
    EXPECT_FALSE(instance2.IsFirstInstance());
  }
  {
    Component instance3;
    EXPECT_FALSE(instance3.IsFirstInstance());
  }
}

}  // namespace
}  // namespace component
}  // namespace forensics
