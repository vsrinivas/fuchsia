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

extern fuchsia::shell::NodeId NullNode;

fuchsia::shell::ShellType TypeUndef();
fuchsia::shell::ShellType TypeBool();
fuchsia::shell::ShellType TypeChar();
fuchsia::shell::ShellType TypeString();
fuchsia::shell::ShellType TypeInt8();
fuchsia::shell::ShellType TypeUint8();
fuchsia::shell::ShellType TypeInt16();
fuchsia::shell::ShellType TypeUint16();
fuchsia::shell::ShellType TypeInt32();
fuchsia::shell::ShellType TypeUint32();
fuchsia::shell::ShellType TypeInt64();
fuchsia::shell::ShellType TypeUint64();
fuchsia::shell::ShellType TypeInteger();
fuchsia::shell::ShellType TypeFloat32();
fuchsia::shell::ShellType TypeFloat64();

// Helper class to create nodes.
class NodeBuilder {
 public:
  explicit NodeBuilder(uint64_t file_id) : file_id_(file_id) {}

  std::vector<fuchsia::shell::NodeDefinition>* nodes() { return &nodes_; }

  // Adds a node definition to the list of nodes.
  fuchsia::shell::NodeId AddNode(fuchsia::shell::Node* node, bool root_node) {
    fuchsia::shell::NodeDefinition node_definition;
    fuchsia::shell::NodeId node_id{file_id_, ++last_node_id_};
    node_definition.node_id = node_id;
    node_definition.node = std::move(*node);
    node_definition.root_node = root_node;
    nodes_.emplace_back(std::move(node_definition));
    return node_id;
  }

  /// Adds an integer literal to the list of nodes.
  fuchsia::shell::NodeId IntegerLiteral(uint64_t absolute_value, bool negative) {
    fuchsia::shell::Node node;
    std::vector<uint64_t> values;
    values.push_back(absolute_value);
    node.set_integer_literal({std::move(values), negative});
    return AddNode(&node, /*root_node=*/false);
  }

  /// Adds an integer literal to the list of nodes.
  fuchsia::shell::NodeId StringLiteral(std::string value) {
    fuchsia::shell::Node node;
    node.set_string_literal(std::move(value));
    return AddNode(&node, /*root_node=*/false);
  }

  // Adds a variable definition to the list of nodes.
  fuchsia::shell::NodeId VariableDefinition(const char* name, fuchsia::shell::ShellType type,
                                            bool mutable_value,
                                            fuchsia::shell::NodeId initial_value,
                                            bool root_node = true) {
    fuchsia::shell::Node node;
    node.set_variable_definition({name, std::move(type), mutable_value, initial_value});
    return AddNode(&node, root_node);
  }

  // Adds a previously defined variable.
  fuchsia::shell::NodeId Variable(fuchsia::shell::NodeId variable) {
    fuchsia::shell::Node node;
    node.set_variable({variable});
    return AddNode(&node, /*root_node=*/false);
  }

 private:
  // The file id for all the nodes built by this builder.
  const uint64_t file_id_;
  // Last value used for a node id.
  uint64_t last_node_id_ = 0;
  // All the nodes which will be sent to the server.
  std::vector<fuchsia::shell::NodeDefinition> nodes_;
};

#endif  // SRC_DEVELOPER_SHELL_INTERPRETER_TEST_INTERPRETER_TEST_H_
