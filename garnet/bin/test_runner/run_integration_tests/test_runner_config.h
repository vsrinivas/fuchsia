// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_TEST_RUNNER_RUN_INTEGRATION_TESTS_TEST_RUNNER_CONFIG_H_
#define GARNET_BIN_TEST_RUNNER_RUN_INTEGRATION_TESTS_TEST_RUNNER_CONFIG_H_

#include <map>
#include <string>
#include <vector>
#include "src/lib/fxl/macros.h"

namespace test_runner {

class TestRunnerConfig {
 public:
  explicit TestRunnerConfig(const std::string& json_path);
  const std::vector<std::string>& test_names() const { return test_names_; }
  const std::vector<std::string>& disabled_test_names() const {
    return disabled_test_names_;
  }
  bool HasTestNamed(const std::string& test_name) const;
  const std::vector<std::string>& GetTestCommand(const std::string& test_name) const;

 private:
  std::vector<std::string> test_names_;
  std::vector<std::string> disabled_test_names_;
  std::map<std::string, std::vector<std::string>> test_commands_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TestRunnerConfig);
};

}  // namespace test_runner

#endif  // GARNET_BIN_TEST_RUNNER_RUN_INTEGRATION_TESTS_TEST_RUNNER_CONFIG_H_
