// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/environment/environment.h"

#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/timekeeper/test_clock.h>

#include "peridot/lib/rng/test_random.h"

namespace ledger {
namespace {

class EnvironmentTest : public ::gtest::TestLoopFixture {
 public:
  sys::testing::ComponentContextProvider component_context_provider_;
};

TEST_F(EnvironmentTest, InitializationOfAsyncAndIOAsync) {
  Environment env =
      EnvironmentBuilder()
          .SetStartupContext(component_context_provider_.context())
          .SetAsync(dispatcher())
          .SetIOAsync(dispatcher())
          .Build();

  EXPECT_EQ(dispatcher(), env.dispatcher());
  EXPECT_EQ(dispatcher(), env.io_dispatcher());
}

TEST_F(EnvironmentTest, InitializationClock) {
  auto clock = std::make_unique<timekeeper::TestClock>();
  auto clock_ptr = clock.get();
  Environment env =
      EnvironmentBuilder()
          .SetStartupContext(component_context_provider_.context())
          .SetAsync(dispatcher())
          .SetIOAsync(dispatcher())
          .SetClock(std::move(clock))
          .Build();

  EXPECT_EQ(clock_ptr, env.clock());
}

TEST_F(EnvironmentTest, InitializationRandom) {
  auto random = std::make_unique<rng::TestRandom>(0);
  auto random_ptr = random.get();
  Environment env =
      EnvironmentBuilder()
          .SetStartupContext(component_context_provider_.context())
          .SetAsync(dispatcher())
          .SetIOAsync(dispatcher())
          .SetRandom(std::move(random))
          .Build();

  EXPECT_EQ(random_ptr, env.random());
}

}  // namespace
}  // namespace ledger
