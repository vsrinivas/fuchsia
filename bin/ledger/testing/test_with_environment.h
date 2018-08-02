// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_TESTING_TEST_WITH_ENVIRONMENT_H_
#define PERIDOT_BIN_LEDGER_TESTING_TEST_WITH_ENVIRONMENT_H_

#include <lib/fit/function.h>
#include <lib/fxl/macros.h>
#include <lib/gtest/test_loop_fixture.h>

#include "peridot/bin/ledger/coroutine/coroutine_impl.h"
#include "peridot/bin/ledger/environment/environment.h"

namespace ledger {

class TestWithEnvironment : public gtest::TestLoopFixture {
 public:
  TestWithEnvironment();

 protected:
  // Runs the given test code in a coroutine.
  void RunInCoroutine(
      fit::function<void(coroutine::CoroutineHandler*)> run_test);

  Environment environment_;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(TestWithEnvironment);
};

}  // namespace ledger

#endif  // PERIDOT_BIN_LEDGER_TESTING_TEST_WITH_ENVIRONMENT_H_
