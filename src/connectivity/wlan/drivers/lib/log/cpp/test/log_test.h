// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_LIB_LOG_CPP_TEST_LOG_TEST_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_LIB_LOG_CPP_TEST_LOG_TEST_H_

#include <string>

#include <gtest/gtest.h>
#include <wlan/drivers/log.h>

namespace wlan::drivers {

#define kDebugTag "dtag"
#define kTraceTag "ttag"

class LogTest : public ::testing::Test {
 public:
  void SetUp() override {
    flag_ = FX_LOG_NONE;
    tag_.clear();
  }

  void ZxlogfEtcOverride(fx_log_severity_t flag, const char* tag, ...) {
    ASSERT_NE(FX_LOG_NONE, flag);
    flag_ = flag;
    if (tag != nullptr) {
      tag_.assign(tag);
    }
  }

  void Validate(fx_log_severity_t flag, const char* tag = nullptr) const {
    ASSERT_EQ(flag_, flag);
    if (tag != nullptr) {
      ASSERT_STREQ(tag_.c_str(), tag);
    }
  }

  bool LogInvoked() const { return (flag_ != FX_LOG_NONE); }

 private:
  fx_log_severity_t flag_;
  std::string tag_;
};
}  // namespace wlan::drivers

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_LIB_LOG_CPP_TEST_LOG_TEST_H_
