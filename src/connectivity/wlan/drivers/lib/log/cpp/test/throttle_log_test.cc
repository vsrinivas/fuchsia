// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "log_test.h"

namespace wlan::drivers {

// Enable all debug levels (i.e. TRACE and above) to allow for full functional testing.
#define WLAN_DRIVER_LOG_LEVEL wlan::drivers::Log::kTRACE

// Ensure no crashes when going via the DDK library.
TEST_F(LogTest, ThrottlefbSanity) {
  Log::SetFilter(0x3);
  lthrottle_error("error throttle %s", "test");
  lthrottle_warn("warn throttle %s", "test");
  lthrottle_info("info throttle %s", "test");
  lthrottle_debug(0x1, kDebugTag, "debug trottle %s", "test");
  lthrottle_trace(0x2, kTraceTag, "trace throttle %s", "test");
}

// The following override is done to ensure the right set of flag and tag is getting passed along.
// Avoid adding tests that require calls to go via DDK library below this.
#ifdef zxlogf_etc
#undef zxlogf_etc
#define zxlogf_etc(flag, tag...) ZxlogfEtcOverride(flag, tag)
#endif

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

#undef WLAN_DRIVER_LOG_LEVEL
#define WLAN_DRIVER_LOG_LEVEL wlan::drivers::Log::kERROR
TEST_F(LogTest, ThrottleLevelError) {
  lthrottle_warn("warn throttle %s", "test");
  lthrottle_info("info throttle %s", "test");
  Log::SetFilter(0x3);
  lthrottle_debug(0x1, kDebugTag, "debug throttle %s", "test");
  lthrottle_trace(0x2, kTraceTag, "trace throttle %s", "test");
  ASSERT_FALSE(LogInvoked());

  lthrottle_error("error throttle %s", "test");
  Validate(DDK_LOG_ERROR);
}

#undef WLAN_DRIVER_LOG_LEVEL
#define WLAN_DRIVER_LOG_LEVEL wlan::drivers::Log::kWARNING
TEST_F(LogTest, ThrottleLevelWarn) {
  lthrottle_info("info throttle %s", "test");
  Log::SetFilter(0x3);
  lthrottle_debug(0x1, kDebugTag, "debug throttle %s", "test");
  lthrottle_trace(0x2, kTraceTag, "trace throttle %s", "test");
  ASSERT_FALSE(LogInvoked());

  lthrottle_error("error throttle %s", "test");
  Validate(DDK_LOG_ERROR);
  lthrottle_warn("warn throttle %s", "test");
  Validate(DDK_LOG_WARNING);
}

#undef WLAN_DRIVER_LOG_LEVEL
#define WLAN_DRIVER_LOG_LEVEL wlan::drivers::Log::kINFO
TEST_F(LogTest, ThrottleLevelInfo) {
  Log::SetFilter(0x3);
  lthrottle_debug(0x1, kDebugTag, "debug throttle %s", "test");
  lthrottle_trace(0x2, kTraceTag, "trace throttle %s", "test");
  ASSERT_FALSE(LogInvoked());

  lthrottle_error("error throttle %s", "test");
  Validate(DDK_LOG_ERROR);
  lthrottle_warn("warn throttle %s", "test");
  Validate(DDK_LOG_WARNING);
  lthrottle_info("info throttle %s", "test");
  Validate(DDK_LOG_INFO);
}

#undef WLAN_DRIVER_LOG_LEVEL
#define WLAN_DRIVER_LOG_LEVEL wlan::drivers::Log::kDEBUG
TEST_F(LogTest, ThrottleLevelDebug) {
  Log::SetFilter(0x3);
  lthrottle_trace(0x2, kTraceTag, "trace throttle %s", "test");
  ASSERT_FALSE(LogInvoked());

  lthrottle_error("error throttle %s", "test");
  Validate(DDK_LOG_ERROR);
  lthrottle_warn("warn throttle %s", "test");
  Validate(DDK_LOG_WARNING);
  lthrottle_info("info throttle %s", "test");
  Validate(DDK_LOG_INFO);
  lthrottle_debug(0x1, kDebugTag, "debug throttle %s", "test");
  Validate(DDK_LOG_DEBUG, kDebugTag);
}

#undef WLAN_DRIVER_LOG_LEVEL
#define WLAN_DRIVER_LOG_LEVEL wlan::drivers::Log::kTRACE
TEST_F(LogTest, ThrottleLevelTrace) {
  Log::SetFilter(0x3);
  lthrottle_error("error throttle %s", "test");
  Validate(DDK_LOG_ERROR);
  lthrottle_warn("warn throttle %s", "test");
  Validate(DDK_LOG_WARNING);
  lthrottle_info("info throttle %s", "test");
  Validate(DDK_LOG_INFO);
  lthrottle_debug(0x1, kDebugTag, "debug throttle %s", "test");
  Validate(DDK_LOG_DEBUG, kDebugTag);
  lthrottle_trace(0x2, kTraceTag, "trace throttle %s", "test");
  Validate(DDK_LOG_TRACE, kTraceTag);
}

}  // namespace wlan::drivers
