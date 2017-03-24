// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_LIB_TESTING_TESTING_H_
#define APPS_MODULAR_LIB_TESTING_TESTING_H_

#include <string>

#include "application/lib/app/application_context.h"
#include "apps/test_runner/services/test_runner.fidl.h"

namespace modular {
namespace testing {

// This connects to the TestRunner service in the caller's
// ApplicationEnvironment. This function must be invoked first before calling
// any of the ones below. A test is expected to call either Done() or
// Teardown() before terminating itself in order for the TestRunner service to
// know that a test process did not crash, or that the test has completed and
// should be torn down.
void Init(app::ApplicationContext* app_context, const std::string& identity);

// Marks the test a failure with the given |log_msg| message, but does not
// teardown; the test may continue running. When the test signals teardown (by
// calling Teardown()), the test is finished as a failure.
void Fail(const std::string& log_msg);

// A test must call Done() before it dies, to let the TestRunner
// service (which has a channel connected to this application) know that this
// test process has not crashed, otherwise it must call Teardown() to signal the
// TestRunner that the test has finished altogether. If Done() is not called and
// the connection to the TestService is broken, the test is declared as failed
// and is torndown. If Done() is called, it is not possible to call Teardown().
void Done();

// A test may call Teardown() to finish the test run and tear down the service.
// Unless Fail() is called, the TestRunner will consider the test run as
// having passed successfully.
void Teardown();

// Signals that this process expects to be terminated within the time specified.
// If it is not killed that is a failure. A test that calls this should not call
// |Done()| or |Teardown()|.
void WillTerminate(double withinSeconds);

// This returns the TestRunnerStore interface from the caller's
// ApplicationEnvironment. Init() must be called before GetStore().
test_runner::TestRunnerStore* GetStore();

namespace internal {

// Register a test point that should pass for a test to be considered
// successful.
void RegisterTestPoint(const std::string& label);

// Signals that a test point has been passed.
void PassTestPoint(const std::string& label);

}  // namespace internal
}  // namespace testing
}  // namespace modular

#endif  // APPS_MODULAR_LIB_TESTING_TESTING_H_
