// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "harness.h"
#include "src/lib/fxl/test/test_settings.h"

#define DISABLED_FOR(b)        \
  if (target_binding == (b)) { \
    return;                    \
  }

namespace {

static const std::string kGo = "go";
static const std::string kHlcpp = "hlcpp";

std::string target_binding = {};

}  // namespace

// NOTE: It is unclear whether we want to migrate this test to the server_suite
// given the vague definition of what it means to observe unbind in a given
// binding.
TEST_F(ServerTest, Bad_ClientClosingChannelCausesUnbind) {
  when([&]() {
    zx_handle_close(client_end);
  }).wait_for([&](auto observations) {
      return observations.has(Observation::Kind::kOnComplete);
    }).then_observe([&](auto observations) {
    // We are not opinionated about what has been observed, just that the last
    // two observations must be unbinding and completion.
    ASSERT_TRUE(2u <= observations.size());
    auto last = observations.size() - 1;
    EXPECT_EQ(Observation::Kind::kOnUnbind, observations[last - 1].kind());
    EXPECT_EQ(Observation::Kind::kOnComplete, observations[last].kind());
  });
}

TEST_F(ClientTest, Good_ServerClosesChannel) {
  // TODO(fxbug.dev/92604): Should work on Go.
  DISABLED_FOR(kGo);

  when([&]() {
    zx_handle_close(server_end);
  }).wait_for([&](auto observations) {
      return 2 <= observations.size();
    }).then_observe([&](auto observations) {
    // Some bindings observe an error, which will precede unbinding.
    ASSERT_TRUE(2u <= observations.size());
    auto last = observations.size() - 1;
    EXPECT_EQ(Observation::Kind::kOnUnbind, observations[last - 1].kind());
    EXPECT_EQ(Observation::Kind::kOnComplete, observations[last].kind());
  });
}

namespace {
static const std::string gtest_enumeration_flag = "--gtest_list_tests";
static const std::string target_flag = "--target";
}  // namespace

int main(int argc, char** argv) {
  // When running this binary in enumeration mode (or discovery mode), the
  // custom arguments specified in the CML are not provided. It is therefore
  // important to identify that are running in this mode to skip any validation
  // we do on arguments.
  for (int i = 1; i < argc; i++) {
    if (argv[i] == gtest_enumeration_flag) {
      goto run_all_tests;
    }
  }

  for (int i = 1; i < argc; i++) {
    if (auto arg = std::string(argv[i]); arg.rfind(target_flag, 0) == 0) {
      target_binding = arg.substr(target_flag.size() + 1);
    }
  }
  if (target_binding != kGo && target_binding != kHlcpp) {
    return EXIT_FAILURE;
  }

run_all_tests:
  if (!fxl::SetTestSettings(argc, argv)) {
    return EXIT_FAILURE;
  }
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
