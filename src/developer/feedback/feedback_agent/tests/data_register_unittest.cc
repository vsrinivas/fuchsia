// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_agent/data_register.h"

#include "src/lib/testing/loop_fixture/test_loop_fixture.h"
#include "third_party/googletest/googlemock/include/gmock/gmock.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace feedback {
namespace {

class DataRegisterTest : public gtest::TestLoopFixture {};

TEST_F(DataRegisterTest, SmokeTest) {
  DataRegister data_register;
  bool called_back = false;
  data_register.Upsert({}, [&called_back]() { called_back = true; });
  RunLoopUntilIdle();
  EXPECT_TRUE(called_back);
}

}  // namespace
}  // namespace feedback
