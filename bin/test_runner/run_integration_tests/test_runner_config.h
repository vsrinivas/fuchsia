// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_TEST_RUNNER_SRC_RUN_INTEGRATION_TESTS_TEST_RUNNER_CONFIG_H_
#define APPS_TEST_RUNNER_SRC_RUN_INTEGRATION_TESTS_TEST_RUNNER_CONFIG_H_

#include <map>
#include <string>
#include <vector>
#include "lib/fxl/macros.h"

namespace test_runner {

class TestRunnerConfig {
 public:
  explicit TestRunnerConfig(const std::string& json_path);

  const std::vector<std::string>& test_names() const { return test_names_; }

  bool HasTestNamed(const std::string& test_name) const {
    return test_commands_.find(test_name) != test_commands_.end();
  }

  const std::string& GetTestCommand(const std::string& test_name) const {
    static const std::string empty;
    const auto& i = test_commands_.find(test_name);
    if (i == test_commands_.end()) {
      return empty;
    }
    return i->second;
  }

 private:
  std::vector<std::string> test_names_;
  std::map<std::string, std::string> test_commands_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TestRunnerConfig);
};

}  // namespace test_runner

#endif  // APPS_TEST_RUNNER_SRC_RUN_INTEGRATION_TESTS_TEST_RUNNER_CONFIG_H_
