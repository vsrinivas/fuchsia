// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_TEST_RUNNER_TEST_RUNNER_CLIENT_H_
#define APPS_MODULAR_TEST_RUNNER_TEST_RUNNER_CLIENT_H_

#include <string>
#include "lib/ftl/macros.h"

namespace modular {
namespace testing {

class TestRunnerClient {
 public:
  TestRunnerClient() {}

  bool RunTest(const std::string& name, const std::string& command_line);

  bool RunTests(const std::string& json_path);

  FTL_DISALLOW_COPY_AND_ASSIGN(TestRunnerClient);
};

}  // namespace testing
}  // namespace modular


#endif  // APPS_MODULAR_TEST_RUNNER_TEST_RUNNER_CLIENT_H_
