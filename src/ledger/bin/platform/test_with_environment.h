// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_PLATFORM_TEST_WITH_ENVIRONMENT_H_
#define SRC_LEDGER_BIN_PLATFORM_TEST_WITH_ENVIRONMENT_H_

#include <lib/fit/function.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/testing/component_context_provider.h>

#include "src/ledger/bin/environment/environment.h"
#include "src/ledger/bin/storage/public/types.h"

namespace ledger {

class TestWithEnvironment : public gtest::TestLoopFixture {
 public:
  // Creates a default test environment.
  TestWithEnvironment();
  TestWithEnvironment(const TestWithEnvironment&) = delete;
  TestWithEnvironment& operator=(const TestWithEnvironment&) = delete;

  // Modifies the default test environment by applying |builder_transformer|.
  TestWithEnvironment(fit::function<void(EnvironmentBuilder*)> builder_transformer);

 protected:
  // Runs the given test code in a coroutine.
  ::testing::AssertionResult RunInCoroutine(
      fit::function<void(coroutine::CoroutineHandler*)> run_test, zx::duration delay = zx::sec(0));

  Environment MakeTestEnvironment(fit::function<void(EnvironmentBuilder*)> builder_transformer);

  sys::testing::ComponentContextProvider component_context_provider_;
  std::unique_ptr<async::LoopInterface> io_loop_interface_;
  Environment environment_;
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_PLATFORM_TEST_WITH_ENVIRONMENT_H_
