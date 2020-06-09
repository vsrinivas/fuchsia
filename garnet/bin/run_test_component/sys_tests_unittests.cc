// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "garnet/bin/run_test_component/sys_tests.h"

namespace {

TEST(SysTests, Simple) {
  for (const auto& url : run::kUrlSet) {
    ASSERT_TRUE(run::should_run_in_sys(url));
  }
}

TEST(SysTests, WithHash) {
  ASSERT_TRUE(run::should_run_in_sys(
      "fuchsia-pkg://fuchsia.com/"
      "timezone-test?hash=3204f2f24920e55bfbcb9c3a058ec2869f229b18d00ef1049ec3f47e5b7e4351#meta/"
      "timezone_bin_test.cmx"));
}

TEST(SysTests, WithVariant) {
  ASSERT_TRUE(run::should_run_in_sys(
      "fuchsia-pkg://fuchsia.com/timezone-test/0#meta/timezone_bin_test.cmx"));
  ASSERT_TRUE(run::should_run_in_sys(
      "fuchsia-pkg://fuchsia.com/timezone-test/1#meta/timezone_bin_test.cmx"));
}

}  // namespace
