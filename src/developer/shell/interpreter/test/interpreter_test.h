// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_SHELL_INTERPRETER_TEST_INTERPRETER_TEST_H_
#define SRC_DEVELOPER_SHELL_INTERPRETER_TEST_INTERPRETER_TEST_H_

#include <map>
#include <memory>
#include <sstream>
#include <vector>

#include "fuchsia/shell/llcpp/fidl.h"
#include "fuchsia/sys/cpp/fidl.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "lib/async-loop/cpp/loop.h"
#include "lib/sys/cpp/component_context.h"
#include "src/developer/shell/console/ast_builder.h"

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
  const std::vector<std::string>& results() const { return results_; }
  bool last_result_partial() const { return last_result_partial_; }

  // Loads a global variable. The loads are defered after the end of the execution.
  void LoadGlobal(const std::string& name) { globals_to_load_.emplace_back(name); }

  // Gets the value for a global variable we loaded using LoadGlobal.
  llcpp::fuchsia::shell::Node* GetGlobal(const std::string& name) {
    auto result = globals_.find(name);
    if (result == globals_.end()) {
      return nullptr;
    }
    return &(result->second->nodes[0]);
  }

  void SetUp() override;

  enum FinishAction { kError, kDump, kExecute, kTextResult };
  void Finish(FinishAction action);

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
  std::vector<std::string> results_;
  bool last_result_partial_ = false;
  // Holds the response buffers for llcpp calls, so that they are cleaned up on shutdown.  Needs to
  // be destructed after globals_.
  std::vector<std::unique_ptr<fidl::Buffer<llcpp::fuchsia::shell::Node>>> to_be_deleted_;
  // Names for the global we will load when the execution will be done.
  std::vector<std::string> globals_to_load_;
  // Holds the values for the globals which have been loaded.
  std::map<std::string, llcpp::fuchsia::shell::Shell::UnownedResultOf::LoadGlobal> globals_;
  // Count of global we are waiting for a result.
  int pending_globals_ = 0;
};

extern shell::console::AstBuilder::NodeId NullNode;

#define ASSERT_CALL_OK(x) ASSERT_EQ(ZX_OK, x.status())

#endif  // SRC_DEVELOPER_SHELL_INTERPRETER_TEST_INTERPRETER_TEST_H_
