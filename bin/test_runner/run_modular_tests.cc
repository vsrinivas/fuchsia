// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This is a simple Fuchsia program that connects to the test_runner process,
// starts a test and exits with success or failure based on the success or
// failure of the test.

#include <iostream>
#include "apps/modular/src/test_runner/test_runner_client.h"

constexpr char kModularTestsJson[] =
    "/system/apps/modular_tests/modular_tests.json";

int main(int argc, char** argv) {
  modular::testing::TestRunnerClient client(kModularTestsJson);

  std::vector<std::string> test_names(argv + 1, argv + argc);
  if (test_names.empty()) {
    // If no tests were specified, run all tests.
    test_names = client.test_names();
  }

  std::vector<std::string> unknown;
  std::vector<std::string> failed;
  std::vector<std::string> succeeded;

  for (auto& test_name : test_names) {
    if (!client.HasTestNamed(test_name)) {
      unknown.push_back(test_name);
      continue;
    }
    if (client.RunTest(test_name)) {
      succeeded.push_back(test_name);
    } else {
      failed.push_back(test_name);
    }
  }

  if (!succeeded.empty()) {
    std::cerr << "Succeeded tests:" << std::endl;
    for (auto& test_name : succeeded) {
      std::cerr << " " << test_name << std::endl;
    }
  }

  if (!failed.empty()) {
    std::cerr << "Failed tests:" << std::endl;
    for (auto& test_name : failed) {
      std::cerr << " " << test_name << std::endl;
    }
  }

  if (!unknown.empty()) {
    std::cerr << "Unknown tests:" << std::endl;
    for (auto& test_name : unknown) {
      std::cerr << " " << test_name << std::endl;
    }
    std::cerr << "Known tests are:" << std::endl;
    for (auto& test_name : client.test_names()) {
      std::cerr << " " << test_name << std::endl;
    }
  }

  if (failed.empty() && unknown.empty()) {
    return 0;
  } else {
    return 1;
  }
}
