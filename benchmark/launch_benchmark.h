// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_BENCHMARK_LAUNCH_BENCHMARK_H_
#define APPS_LEDGER_BENCHMARK_LAUNCH_BENCHMARK_H_

#include <vector>

#include "application/lib/app/application_context.h"

// Benchmark that executes a given app multiple times, each with a different
// value for a given test argument.
//
// Parameters:
//   --app=<app url> the url of the benchmark app to be executed
//   --test-arg=<argument to test> the argument of the app to be tested
//   --min-value=<int> the initial (minimum) value for the test-arg
//   --max-value=<int> the final (maximum) value for the test-arg
//   --step=<int> the step increasing the test-arg value after each execution
//   --append-args=<args> comma separated additional arguments for the app
class LaunchBenchmark {
 public:
  LaunchBenchmark(std::string app_url,
                  std::string test_arg,
                  int min_value,
                  int max_value,
                  int step,
                  std::vector<std::string> args);

  void StartNext();

 private:
  std::string app_url_;
  std::string test_arg_;
  int current_value_;
  int max_value_;
  int step_;
  std::vector<std::string> args_;

  std::unique_ptr<app::ApplicationContext> context_;
  app::ApplicationControllerPtr application_controller_;
};

#endif  // APPS_LEDGER_BENCHMARK_LAUNCH_BENCHMARK_H_
