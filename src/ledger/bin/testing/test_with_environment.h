// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_TESTING_TEST_WITH_ENVIRONMENT_H_
#define SRC_LEDGER_BIN_TESTING_TEST_WITH_ENVIRONMENT_H_

#include <lib/component/cpp/startup_context.h>
#include <lib/fit/function.h>
#include <src/lib/fxl/macros.h>
#include <lib/gtest/test_loop_fixture.h>

#include "src/ledger/bin/environment/environment.h"

namespace ledger {

class TestWithEnvironment : public gtest::TestLoopFixture {
 public:
  TestWithEnvironment();

 protected:
  // Runs the given test code in a coroutine.
  void RunInCoroutine(
      fit::function<void(coroutine::CoroutineHandler*)> run_test);

  std::unique_ptr<component::StartupContext> startup_context_;
  Environment environment_;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(TestWithEnvironment);
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_TESTING_TEST_WITH_ENVIRONMENT_H_
