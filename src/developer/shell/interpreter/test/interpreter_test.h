// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_SHELL_INTERPRETER_TEST_INTERPRETER_TEST_H_
#define SRC_DEVELOPER_SHELL_INTERPRETER_TEST_INTERPRETER_TEST_H_

#include <map>
#include <memory>
#include <sstream>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "fuchsia/shell/llcpp/fidl.h"
#include "fuchsia/sys/cpp/fidl.h"
#include "lib/async-loop/cpp/loop.h"
#include "lib/sys/cpp/component_context.h"
#include "src/developer/shell/common/ast_builder.h"
#include "src/developer/shell/common/result.h"

shell::console::AstBuilder::NodePair AddObject(
    shell::console::AstBuilder& builder, std::vector<std::string>& names,
    std::vector<shell::console::AstBuilder::NodeId>& values,
    std::vector<llcpp::fuchsia::shell::ShellType>&& types);

struct InterpreterTestContext {
  InterpreterTestContext(uint64_t new_id) : id(new_id) {}

  // Returns the result. If the error stream is not empty, prints it.
  llcpp::fuchsia::shell::ExecuteResult GetResult() const;

  uint64_t id;
  llcpp::fuchsia::shell::ExecuteResult result = llcpp::fuchsia::shell::ExecuteResult::UNDEF;
  std::stringstream error_stream;
};

class InterpreterTest : public ::testing::Test {
 public:
  InterpreterTest();

 protected:
  std::string GlobalErrors() { return global_error_stream_.str(); }
  llcpp::fuchsia::shell::Shell::SyncClient& shell() { return *(shell_.get()); }
  const std::vector<std::string>& text_results() const { return text_results_; }
  bool last_text_result_partial() const { return last_text_result_partial_; }
  const std::vector<std::unique_ptr<shell::common::ResultNode>>& results() const {
    return results_;
  }

  void SetUp() override;

  enum FinishAction { kError, kDump, kExecute, kTextResult };
  // Execute the action and then shutdown the interpreter. After that, we can't do anything else.
  void Finish(FinishAction action);
  void Finish(FinishAction action, const std::vector<std::string>& expected_errors);
  // Execute the action. We can have several calls to Run. The last action must be executed using
  // Finish.
  void Run(FinishAction action);

  InterpreterTestContext* CreateContext();
  InterpreterTestContext* GetContext(uint64_t context_id);

 private:
  async::Loop loop_;
  std::unique_ptr<sys::ComponentContext> context_;
  fuchsia::sys::ComponentControllerPtr controller_;
  std::unique_ptr<sys::ServiceDirectory> shell_provider_;
  std::unique_ptr<llcpp::fuchsia::shell::Shell::SyncClient> shell_;

  uint64_t last_context_id_ = 0;
  std::map<uint64_t, std::unique_ptr<InterpreterTestContext>> contexts_;
  std::stringstream global_error_stream_;
  std::vector<std::string> text_results_;
  bool last_text_result_partial_ = false;
  std::vector<std::unique_ptr<shell::common::ResultNode>> results_;
};

extern shell::console::AstBuilder::NodeId NullNode;

#define ASSERT_CALL_OK(x) ASSERT_EQ(ZX_OK, x.status())

#define CHECK_RESULT(index, string)                          \
  {                                                          \
    ASSERT_LT(static_cast<size_t>(index), results().size()); \
    std::stringstream ss;                                    \
    results()[index]->Dump(ss);                              \
    ASSERT_EQ(string, ss.str());                             \
  }

#endif  // SRC_DEVELOPER_SHELL_INTERPRETER_TEST_INTERPRETER_TEST_H_
