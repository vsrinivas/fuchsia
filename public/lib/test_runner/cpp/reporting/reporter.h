// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_TEST_RUNNER_LIB_REPORTER_H_
#define APPS_TEST_RUNNER_LIB_REPORTER_H_

#include "lib/app/cpp/application_context.h"
#include "lib/test_runner/fidl/test_runner.fidl-sync.h"
#include "lib/fidl/cpp/bindings/synchronous_interface_ptr.h"

namespace test_runner {

// Connects to the TestRunner service and reports test results.
class Reporter {
 public:
  explicit Reporter(std::string identity);

  ~Reporter();

  void Start(app::ApplicationContext* context);

  // Reports a test result.
  void Report(TestResultPtr result);

  // Return whether the connection to TestRunner was successful.
  bool connected();

 private:
  // Reports teardown to the TestRunner service and waits for acknoledgement.
  void Stop();

  std::string identity_;
  fidl::SynchronousInterfacePtr<TestRunner> test_runner_;
};

}  // namespace test_runner

#endif  // APPS_TEST_RUNNER_LIB_REPORTER_H_
