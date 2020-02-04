// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_SHELL_INTERPRETER_TEST_INTERPRETER_TEST_H_
#define SRC_DEVELOPER_SHELL_INTERPRETER_TEST_INTERPRETER_TEST_H_

#include <map>
#include <memory>
#include <sstream>

#include "fuchsia/shell/cpp/fidl.h"
#include "fuchsia/sys/cpp/fidl.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "lib/async-loop/cpp/loop.h"
#include "lib/sys/cpp/component_context.h"

struct InterpreterTestContext {
  InterpreterTestContext(uint64_t new_id) : id(new_id) {}

  uint64_t id;
  fuchsia::shell::ExecuteResult result = fuchsia::shell::ExecuteResult::UNDEF;
  std::stringstream error_stream;
};

class InterpreterTest : public ::testing::Test {
 public:
  InterpreterTest();

 protected:
  std::string GlobalErrors() { return global_error_stream_.str(); }

  void SetUp() override;

  void Run() { loop_.Run(); }
  void Quit() { loop_.Quit(); }

  InterpreterTestContext* CreateContext();
  InterpreterTestContext* GetContext(uint64_t context_id);

  fuchsia::shell::ShellPtr& shell() { return shell_; }

 private:
  async::Loop loop_;
  std::unique_ptr<sys::ComponentContext> context_;
  fuchsia::sys::ComponentControllerPtr controller_;
  std::unique_ptr<sys::ServiceDirectory> shell_provider_;
  fuchsia::shell::ShellPtr shell_;
  uint64_t last_context_id_ = 0;
  std::map<uint64_t, std::unique_ptr<InterpreterTestContext>> contexts_;
  std::stringstream global_error_stream_;
};

#endif  // SRC_DEVELOPER_SHELL_INTERPRETER_TEST_INTERPRETER_TEST_H_
