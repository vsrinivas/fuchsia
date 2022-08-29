// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <wlan/drivers/internal/common.h>

#include "log_test.h"

namespace wlan::drivers {

// Ensure no crashes when going via the DDK library.
TEST_F(LogTest, ThrottlefbSanity) {
  Log::SetFilter(0x3);
  lthrottle_error("error throttle %s", "test");
  lthrottle_warn("warn throttle %s", "test");
  lthrottle_info("info throttle %s", "test");
  lthrottle_debug(0x1, kDebugTag, "debug trottle %s", "test");
  lthrottle_trace(0x2, kTraceTag, "trace throttle %s", "test");
}

TEST_F(LogTest, ThrottleError) {
  lthrottle_error("error throttle %s", "test");
  Validate(DDK_LOG_ERROR);
}

TEST_F(LogTest, ThrottleWarn) {
  lthrottle_warn("warn throttle %s", "test");
  Validate(DDK_LOG_WARNING);
}

TEST_F(LogTest, ThrottleInfo) {
  lthrottle_info("info throttle %s", "test");
  Validate(DDK_LOG_INFO);
}

TEST_F(LogTest, ThrottleDebugFiltered) {
  Log::SetFilter(0);
  lthrottle_debug(0x1, kDebugTag, "debug throttle %s", "test");
  ASSERT_FALSE(LogInvoked());
}

TEST_F(LogTest, ThrottleDebugNotFiltered) {
  Log::SetFilter(0x1);
  lthrottle_debug(0x1, kDebugTag, "debug throttle %s", "test");
  ASSERT_TRUE(LogInvoked());
  Validate(DDK_LOG_DEBUG, kDebugTag);
}

TEST_F(LogTest, ThrottleTraceFiltered) {
  Log::SetFilter(0);
  lthrottle_trace(0x2, kTraceTag, "trace throttle %s", "test");
  ASSERT_FALSE(LogInvoked());
}

TEST_F(LogTest, ThrottleTraceNotFiltered) {
  Log::SetFilter(0x2);
  lthrottle_trace(0x2, kTraceTag, "trace throttle %s", "test");
  ASSERT_TRUE(LogInvoked());
  Validate(DDK_LOG_TRACE, kTraceTag);
}

TEST_F(LogTest, ThrottleLogIf) {
  lthrottle_log_if(1, false, lerror("hello"));
  ASSERT_FALSE(LogInvoked());

  lthrottle_log_if(1, true, lwarn("hello2"));
  Validate(DDK_LOG_WARNING);
}

}  // namespace wlan::drivers
