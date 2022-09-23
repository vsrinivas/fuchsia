// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <wlan/drivers/log_instance.h>

#include "log_test.h"
#include "zx_ticks_override.h"

namespace wlan::drivers {

#define VALIDATE_THROTTLE(level, log)              \
  do {                                             \
    for (size_t i = 0; i < 3; i++) {               \
      Reset();                                     \
      log;                                         \
      if (is_even(i)) {                            \
        ASSERT_TRUE(LogInvoked());                 \
        Validate(level);                           \
      } else {                                     \
        ASSERT_FALSE(LogInvoked());                \
        zx_ticks_increment(zx_ticks_per_second()); \
      }                                            \
    }                                              \
  } while (0);

bool is_even(size_t i) { return (i % 2) == 0; }

// Ensure no crashes when going via the DDK library.
TEST_F(LogTest, ThrottlefbSanity) {
  log::Instance::Init(0x3);
  lthrottle_error("error throttle %s", "test");
  lthrottle_warn("warn throttle %s", "test");
  lthrottle_info("info throttle %s", "test");
  lthrottle_debug(0x1, kDebugTag, "debug trottle %s", "test");
  lthrottle_trace(0x2, kTraceTag, "trace throttle %s", "test");
}

TEST_F(LogTest, ThrottleError) {
  VALIDATE_THROTTLE(DDK_LOG_ERROR, lthrottle_error("error %s", "test"));
}

TEST_F(LogTest, ThrottleWarn) {
  VALIDATE_THROTTLE(DDK_LOG_WARNING, lthrottle_warn("warn %s", "test"));
}

TEST_F(LogTest, ThrottleInfo) {
  VALIDATE_THROTTLE(DDK_LOG_INFO, lthrottle_info("info %s", "test"));
}

TEST_F(LogTest, ThrottleDebugFiltered) {
  log::Instance::Init(0);
  lthrottle_debug(0x1, kDebugTag, "debug throttle %s", "test");
  ASSERT_FALSE(LogInvoked());
}

TEST_F(LogTest, ThrottleDebugNotFiltered) {
  log::Instance::Init(0x1);
  VALIDATE_THROTTLE(DDK_LOG_DEBUG, lthrottle_debug(0x1, kDebugTag, "debug %s", "test"));
}

TEST_F(LogTest, ThrottleTraceFiltered) {
  log::Instance::Init(0);
  lthrottle_trace(0x2, kTraceTag, "trace throttle %s", "test");
  ASSERT_FALSE(LogInvoked());
}

TEST_F(LogTest, ThrottleTraceNotFiltered) {
  log::Instance::Init(0x2);
  VALIDATE_THROTTLE(DDK_LOG_TRACE, lthrottle_trace(0x2, kTraceTag, "trace %s", "test"));
}

TEST_F(LogTest, ThrottleLogIf) {
  lthrottle_log_if(1, false, lerror("hello"));
  ASSERT_FALSE(LogInvoked());

  lthrottle_log_if(1, true, lwarn("hello2"));
  Validate(DDK_LOG_WARNING);
}

}  // namespace wlan::drivers
