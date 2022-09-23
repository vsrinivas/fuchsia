// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <wlan/drivers/log_instance.h>

#include "log_test.h"

namespace wlan::drivers {

TEST(FilterTest, SingleBit) {
  log::Instance::Init(0x2);
  EXPECT_TRUE(log::Instance::IsFilterOn(0x2));
  EXPECT_FALSE(log::Instance::IsFilterOn(~0x2));

  log::Instance::Init(0x8000);
  EXPECT_TRUE(log::Instance::IsFilterOn(0x8000));
  EXPECT_FALSE(log::Instance::IsFilterOn(~0x8000));
}

TEST(FilterTest, MultiBit) {
  log::Instance::Init(0xF);
  EXPECT_TRUE(log::Instance::IsFilterOn(0x1));
  EXPECT_TRUE(log::Instance::IsFilterOn(0x2));
  EXPECT_TRUE(log::Instance::IsFilterOn(0x4));
  EXPECT_TRUE(log::Instance::IsFilterOn(0x8));
  EXPECT_FALSE(log::Instance::IsFilterOn(~0xF));
}

// Ensure no crashes when going via the DDK library.
TEST_F(LogTest, Sanity) {
  lerror("error %s", "test");
  lwarn("warn %s", "test");
  linfo("info %s", "test");
  log::Instance::Init(0x3);
  ldebug(0x1, kDebugTag, "debug %s", "test");
  ltrace(0x2, kTraceTag, "trace %s", "test");
}

TEST_F(LogTest, Error) {
  lerror("error %s", "test");
  Validate(DDK_LOG_ERROR);
}

TEST_F(LogTest, Warn) {
  lwarn("warn %s", "test");
  Validate(DDK_LOG_WARNING);
}

TEST_F(LogTest, Info) {
  linfo("info %s", "test");
  Validate(DDK_LOG_INFO);
}

TEST_F(LogTest, DebugFiltered) {
  log::Instance::Init(0);
  ldebug(0x1, kDebugTag, "debug %s", "test");
  ASSERT_FALSE(LogInvoked());
}

TEST_F(LogTest, DebugNotFiltered) {
  log::Instance::Init(0x1);
  ldebug(0x1, kDebugTag, "debug %s", "test");
  ASSERT_TRUE(LogInvoked());
  Validate(DDK_LOG_DEBUG, kDebugTag);
}

TEST_F(LogTest, TraceFiltered) {
  log::Instance::Init(0);
  ltrace(0x2, kTraceTag, "trace %s", "test");
  ASSERT_FALSE(LogInvoked());
}

TEST_F(LogTest, TraceNotFiltered) {
  log::Instance::Init(0x2);
  ltrace(0x2, kTraceTag, "trace %s", "test");
  ASSERT_TRUE(LogInvoked());
  Validate(DDK_LOG_TRACE, kTraceTag);
}

}  // namespace wlan::drivers
