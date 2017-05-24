// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/lib/tests/test_with_message_loop.h"

#include "apps/test_runner/services/test_runner.fidl.h"
#include "lib/ftl/functional/make_copyable.h"

namespace mozart {
namespace test {

int RunTestsWithMessageLoopAndTestRunner(
    std::string tests_name,
    std::function<int(app::ApplicationContext*)> run_tests) {
  mtl::MessageLoop message_loop;
  auto application_context = app::ApplicationContext::CreateFromStartupInfo();

  // TODO(vardhan,bgoldman): These tests shouldn't have to deal with this.
  // figure out how to hide all of this.
  test_runner::TestRunnerPtr test_runner =
      application_context
          ->ConnectToEnvironmentService<test_runner::TestRunner>();

  // Assume we are using TestRunner until proven otherwise.
  bool using_test_runner = true;
  bool tests_finished = false;

  // We get a connection error if we are not using TestRunner. Use different
  // teardown logic in that case.
  test_runner.set_connection_error_handler(
      [&using_test_runner, &tests_finished] {
        using_test_runner = false;
        if (tests_finished) {
          mtl::MessageLoop::GetCurrent()->PostQuitTask();
        }
        // If tests are not finished, we call PostQuitTask later on.
      });
  test_runner->Identify("mozart_view_manager_tests");

  int status = run_tests(application_context.get());

  tests_finished = true;
  if (using_test_runner) {
    if (status != 0) {
      test_runner->Fail("Failed");
    }
    test_runner->Teardown(
        [] { mtl::MessageLoop::GetCurrent()->PostQuitTask(); });
  } else {
    mtl::MessageLoop::GetCurrent()->PostQuitTask();
  }
  message_loop.Run();
  return status;
}

bool TestWithMessageLoop::RunLoopWithTimeout(ftl::TimeDelta timeout) {
  auto canceled = std::make_unique<bool>(false);
  bool* canceled_ptr = canceled.get();
  bool timed_out = false;
  mtl::MessageLoop::GetCurrent()->task_runner()->PostDelayedTask(
      ftl::MakeCopyable([ this, canceled = std::move(canceled), &timed_out ] {
        if (*canceled) {
          return;
        }
        timed_out = true;
        mtl::MessageLoop::GetCurrent()->QuitNow();
      }),
      timeout);
  mtl::MessageLoop::GetCurrent()->Run();
  if (!timed_out) {
    *canceled_ptr = true;
  }
  return timed_out;
}

}  // namespace test
}  // namespace mozart
