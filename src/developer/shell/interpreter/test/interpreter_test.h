// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_SHELL_INTERPRETER_TEST_INTERPRETER_TEST_H_
#define SRC_DEVELOPER_SHELL_INTERPRETER_TEST_INTERPRETER_TEST_H_

#include <map>
#include <memory>
#include <sstream>
#include <vector>

#include "fuchsia/shell/cpp/fidl.h"
#include "fuchsia/sys/cpp/fidl.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "lib/async-loop/cpp/loop.h"
#include "lib/sys/cpp/component_context.h"

struct InterpreterTestContext {
  InterpreterTestContext(uint64_t new_id) : id(new_id) {}

  // Returns the result. If the error stream is not empty, prints it.
  fuchsia::shell::ExecuteResult GetResult() const;

  uint64_t id;
  fuchsia::shell::ExecuteResult result = fuchsia::shell::ExecuteResult::UNDEF;
  std::stringstream error_stream;
};

class InterpreterTest : public ::testing::Test {
 public:
  InterpreterTest();

 protected:
  std::string GlobalErrors() { return global_error_stream_.str(); }
  fuchsia::shell::ShellPtr& shell() { return shell_; }
  const std::vector<std::string>& results() const { return results_; }
  bool last_result_partial() const { return last_result_partial_; }

  // Loads a global variable. The loads are defered after the end of the execution.
  void LoadGlobal(const std::string& name) { globals_to_load_.emplace_back(name); }

  // Gets the value for a global variable we loaded using LoadGlobal.
  fuchsia::shell::Node* GetGlobal(const std::string& name) {
    auto result = globals_.find(name);
    if (result == globals_.end()) {
      return nullptr;
    }
    return &result->second;
  }

  void SetUp() override;

  void Run() {
    running_ = true;
    loop_.Run();
  }
  void Quit() {
    running_ = false;
    loop_.Quit();
  }

  InterpreterTestContext* CreateContext();
  InterpreterTestContext* GetContext(uint64_t context_id);

 private:
  async::Loop loop_;
  std::unique_ptr<sys::ComponentContext> context_;
  fuchsia::sys::ComponentControllerPtr controller_;
  std::unique_ptr<sys::ServiceDirectory> shell_provider_;
  fuchsia::shell::ShellPtr shell_;
  uint64_t last_context_id_ = 0;
  std::map<uint64_t, std::unique_ptr<InterpreterTestContext>> contexts_;
  std::stringstream global_error_stream_;
  std::vector<std::string> results_;
  bool last_result_partial_ = false;
  bool running_ = false;
  // Names for the global we will load when the execution will be done.
  std::vector<std::string> globals_to_load_;
  // Holds the values for the globals which have been loaded.
  std::map<std::string, fuchsia::shell::Node> globals_;
  // Count of global we are waiting for a result.
  int pending_globals_ = 0;
};

#endif  // SRC_DEVELOPER_SHELL_INTERPRETER_TEST_INTERPRETER_TEST_H_
