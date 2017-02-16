// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_TEST_RUNNER_TEST_RUNNER_CLIENT_H_
#define APPS_MODULAR_TEST_RUNNER_TEST_RUNNER_CLIENT_H_

#include <map>
#include <string>
#include <vector>
#include "lib/ftl/macros.h"

namespace modular {
namespace testing {

class TestRunnerClient {
 public:
  TestRunnerClient() {}
  TestRunnerClient(const std::string& json_path);

  const std::vector<std::string>& test_names() const { return test_names_; }

  bool HasTestNamed(const std::string& test_name) const {
    return test_commands_.find(test_name) != test_commands_.end();
  }

  bool RunTest(const std::string& name);
  bool RunAllTests();

 private:
  std::vector<std::string> test_names_;
  std::map<std::string, std::string> test_commands_;

  FTL_DISALLOW_COPY_AND_ASSIGN(TestRunnerClient);
};

}  // namespace testing
}  // namespace modular

#endif  // APPS_MODULAR_TEST_RUNNER_TEST_RUNNER_CLIENT_H_
