// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_TEST_BENCHMARK_LAUNCH_BENCHMARK_H_
#define PERIDOT_BIN_LEDGER_TEST_BENCHMARK_LAUNCH_BENCHMARK_H_

#include <vector>

#include "lib/app/cpp/application_context.h"

// Benchmark that executes a given app multiple times, each with a different
// value for a given test argument.
//
// Parameters:
//   --app=<app url> the url of the benchmark app to be executed
//   --test-arg=<argument to test> the argument of the app to be tested
//   --min-value=<int> the initial (minimum) value for the test-arg
//   --max-value=<int> the final (maximum) value for the test-arg
//   --step=<int> used for arithmetic sequence updates in the value: the
//     test-arg value will be increased by |step| after each execution
//   --mult=<int> used for geometric sequence updates in the value: the test-arg
//     value will be multiplied by |mult| after each execution
//   --append-args=<args> comma separated additional arguments for the app
class LaunchBenchmark {
 public:
  enum class SequenceType { ARITHMETIC, GEOMETRIC };

  LaunchBenchmark(std::string app_url,
                  std::string test_arg,
                  int min_value,
                  int max_value,
                  SequenceType sequence_type,
                  int step,
                  std::vector<std::string> args);

  void StartNext();

 private:
  std::string app_url_;
  std::string test_arg_;
  int current_value_;
  int max_value_;
  SequenceType sequence_type_;
  int step_;
  std::vector<std::string> args_;

  std::unique_ptr<app::ApplicationContext> context_;
  app::ApplicationControllerPtr application_controller_;
};

#endif  // PERIDOT_BIN_LEDGER_TEST_BENCHMARK_LAUNCH_BENCHMARK_H_
