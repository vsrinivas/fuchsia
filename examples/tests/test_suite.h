// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXAMPLES_TESTS_TEST_SUITE_H_
#define EXAMPLES_TESTS_TEST_SUITE_H_

#include <fuchsia/test/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding.h>

namespace example {

struct Options {
  bool dont_service_get_tests = false;
  bool dont_service_run = false;
  bool close_channel_get_tests = false;
  bool close_channel_run = false;
};

struct TestInput {
  std::string name;
  fuchsia::test::Status status;
  /// Skips OnTestCaseFinished if true
  bool incomplete_test = false;
  // will not set status if false.
  bool set_result_status = true;
};

class TestSuite : public fuchsia::test::Suite {
 public:
  explicit TestSuite(async::Loop* loop, std::vector<TestInput> inputs, Options options = Options{});

  void GetTests(GetTestsCallback callback) override;

  void Run(std::vector<fuchsia::test::Invocation> tests, fuchsia::test::RunOptions /*unused*/,
           fidl::InterfaceHandle<fuchsia::test::RunListener> run_listener) override;

  fidl::InterfaceRequestHandler<fuchsia::test::Suite> GetHandler();

 private:
  fidl::Binding<fuchsia::test::Suite> binding_;
  std::vector<TestInput> test_inputs_;
  Options options_;
  async::Loop* loop_;
};

}  // namespace example

#endif  // EXAMPLES_TESTS_TEST_SUITE_H_
