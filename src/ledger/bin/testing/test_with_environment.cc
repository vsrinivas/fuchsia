// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/testing/test_with_environment.h"

#include <lib/fit/function.h>

#include "peridot/lib/rng/test_random.h"
#include "src/ledger/bin/app/flags.h"
#include "src/ledger/bin/environment/test_loop_notification.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/bin/testing/run_in_coroutine.h"
#include "src/lib/timekeeper/test_loop_test_clock.h"

namespace ledger {

TestWithEnvironment::TestWithEnvironment()
    : TestWithEnvironment([](EnvironmentBuilder* builder) {}) {}

TestWithEnvironment::TestWithEnvironment(
    fit::function<void(EnvironmentBuilder*)> builder_transformer)
    : io_loop_interface_(test_loop().StartNewLoop()),
      environment_(MakeTestEnvironment(std::move(builder_transformer))) {}

Environment TestWithEnvironment::MakeTestEnvironment(
    fit::function<void(EnvironmentBuilder*)> builder_transformer) {
  EnvironmentBuilder builder;
  builder.SetAsync(dispatcher())
      .SetIOAsync(io_loop_interface_->dispatcher())
      .SetNotificationFactory(TestLoopNotification::NewFactory(&test_loop()))
      .SetStartupContext(component_context_provider_.context())
      .SetClock(std::make_unique<timekeeper::TestLoopTestClock>(&test_loop()))
      .SetRandom(std::make_unique<rng::TestRandom>(test_loop().initial_state()))
      .SetGcPolicy(kTestingGarbageCollectionPolicy);
  builder_transformer(&builder);
  return builder.Build();
}

::testing::AssertionResult TestWithEnvironment::RunInCoroutine(
    fit::function<void(coroutine::CoroutineHandler*)> run_test, zx::duration delay) {
  bool completed = ::ledger::RunInCoroutine(&test_loop(), environment_.coroutine_service(),
                                            std::move(run_test), delay);
  if (!completed) {
    return ::testing::AssertionFailure() << "Coroutine stopped executing but did not end.";
  }
  return ::testing::AssertionSuccess();
}

}  // namespace ledger
