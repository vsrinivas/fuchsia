// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/shell/interpreter/test/interpreter_test.h"

#include <lib/fdio/directory.h>

#include <iostream>
#include <sstream>
#include <string>

#include "fuchsia/shell/llcpp/fidl.h"
#include "fuchsia/sys/cpp/fidl.h"
#include "lib/async-loop/default.h"
#include "zircon/status.h"

llcpp::fuchsia::shell::ExecuteResult InterpreterTestContext::GetResult() const {
  std::string string = error_stream.str();
  if (!string.empty()) {
    std::cout << string;
  }
  return result;
}

InterpreterTest::InterpreterTest()
    : loop_(&kAsyncLoopConfigAttachToCurrentThread),
      context_(sys::ComponentContext::CreateAndServeOutgoingDirectory()) {
  ::fidl::InterfaceHandle<fuchsia::io::Directory> directory;

  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = "fuchsia-pkg://fuchsia.com/shell_server#meta/shell_server.cmx";
  launch_info.directory_request = directory.NewRequest().TakeChannel();

  fuchsia::sys::LauncherPtr launcher;
  context_->svc()->Connect(launcher.NewRequest());
  launcher->CreateComponent(std::move(launch_info), controller_.NewRequest());

  shell_provider_ = std::make_unique<sys::ServiceDirectory>(std::move(directory));
}

void InterpreterTest::Finish(FinishAction action) {
  std::vector<std::string> no_errors;
  Finish(action, no_errors);
}

void InterpreterTest::Finish(FinishAction action, const std::vector<std::string>& expected_errors) {
  Run(action);
  // Shutdown the interpreter (that also closes the channel => we can't use it anymore after this
  // call).
  auto errors = shell().Shutdown();
  // Checks if the errors are what we expected.
  bool ok = true;
  if (expected_errors.size() != errors->errors.count()) {
    ok = false;
  } else {
    for (size_t i = 0; i < expected_errors.size(); ++i) {
      if (expected_errors[i] != std::string(errors->errors[i].data(), errors->errors[i].size())) {
        ok = false;
        break;
      }
    }
  }
  if (!ok) {
    std::cout << "Shutdown incorrect\n";
    if (!expected_errors.empty()) {
      std::cout << "Expected:\n";
      for (const auto& error : expected_errors) {
        std::cout << "  " << error << '\n';
      }
      if (errors->errors.empty()) {
        std::cout << "Got no error\n";
      } else {
        std::cout << "Got:\n";
      }
    }
    for (const auto& error : errors->errors) {
      std::cout << "  " << std::string(error.data(), error.size()) << '\n';
    }
    ASSERT_TRUE(ok);
  }
}

void InterpreterTest::Run(FinishAction action) {
  llcpp::fuchsia::shell::Shell::EventHandlers handlers;
  bool done = false;
  enum Errs : zx_status_t { kNoContext = 1, kNoResult, kWrongAction };
  while (!done) {
    std::string msg;
    handlers.on_error = [this, &msg, &done, action](
                            uint64_t context_id,
                            fidl::VectorView<llcpp::fuchsia::shell::Location> locations,
                            fidl::StringView error_message) -> zx_status_t {
      if (action == kError) {
        done = true;
      }
      if (context_id == 0) {
        global_error_stream_ << std::string(error_message.data(), error_message.size()) << "\n";
      } else {
        InterpreterTestContext* context = GetContext(context_id);
        if (context == nullptr) {
          msg = "context == nullptr in on_error";
          return kNoContext;
        }
        for (const auto& location : locations) {
          if (location.has_node_id()) {
            context->error_stream << "node " << location.node_id().file_id << ':'
                                  << location.node_id().node_id << ' ';
          }
        }
        context->error_stream << std::string(error_message.data(), error_message.size()) << "\n";
      }
      return ZX_OK;
    };

    handlers.on_dump_done = [this, &done, &msg, action](uint64_t context_id) -> zx_status_t {
      if (action == kDump) {
        done = true;
      }
      InterpreterTestContext* context = GetContext(context_id);
      if (context == nullptr) {
        msg = "context == nullptr in on_dump_done";
        return kNoContext;
      }
      return ZX_OK;
    };

    handlers.on_execution_done = [this, &msg, &done, action](
                                     uint64_t context_id,
                                     llcpp::fuchsia::shell::ExecuteResult result) -> zx_status_t {
      if (action != kExecute) {
        msg = "Expected action: kExecute was: " + std::to_string(action);
        return kWrongAction;
      }
      done = true;

      InterpreterTestContext* context = GetContext(context_id);
      if (context == nullptr) {
        msg = "context == nullptr in on_execution_done";
        return kNoContext;
      }
      context->result = result;
      if (!globals_to_load_.empty() && (result == llcpp::fuchsia::shell::ExecuteResult::OK)) {
        // Now that the execution is finished, loads all the global variables we asked using
        // LoadGlobal.
        for (const auto& global : globals_to_load_) {
          ++pending_globals_;
          fidl::Buffer<llcpp::fuchsia::shell::Node> request_buffer;
          auto& response_buffer =
              to_be_deleted_.emplace_back(new fidl::Buffer<llcpp::fuchsia::shell::Node>());
          auto response = shell().LoadGlobal(request_buffer.view(), fidl::unowned_str(global),
                                             response_buffer->view());
          auto& nodes = response->nodes;
          if (!nodes.empty()) {
            globals_.emplace(global, std::move(response));
          }
        }
      }
      return ZX_OK;
    };

    handlers.on_text_result = [this, &msg, &done, action](uint64_t context_id,
                                                          fidl::StringView result,
                                                          bool partial_result) -> zx_status_t {
      if (action == kTextResult) {
        done = true;
      }
      InterpreterTestContext* context = GetContext(context_id);
      if (context == nullptr) {
        msg = "context == nullptr in on_text_result";
        return kNoContext;
      }
      std::string result_string(result.data(), result.size());
      if (last_result_partial_) {
        if (results_.empty()) {
          msg = "results empty";
          return kNoResult;
        }
        results_.back() += result_string;
      } else {
        results_.emplace_back(std::move(result_string));
      }
      last_result_partial_ = partial_result;
      return ZX_OK;
    };
    ASSERT_EQ(ZX_OK, shell_->HandleEvents(std::move(handlers))) << msg;
  }
}

InterpreterTestContext* InterpreterTest::CreateContext() {
  uint64_t id = ++last_context_id_;
  auto context = std::make_unique<InterpreterTestContext>(id);
  auto result = context.get();
  contexts_.emplace(id, std::move(context));
  return result;
}

InterpreterTestContext* InterpreterTest::GetContext(uint64_t context_id) {
  auto result = contexts_.find(context_id);
  if (result == contexts_.end()) {
    return nullptr;
  }
  return result->second.get();
}

// Checks that the given node is an integer literal of the given value.  Asserts on failure.
void NodeIsInteger(const llcpp::fuchsia::shell::Node* node, uint64_t val, bool negative) {
  ASSERT_TRUE(node->is_integer_literal());
  ASSERT_EQ(negative, node->integer_literal().negative);
  ASSERT_EQ(static_cast<size_t>(1), node->integer_literal().absolute_value.count());
  ASSERT_EQ(val, node->integer_literal().absolute_value[0]);
}

// Check that there is a variable with the given name, and it is a wire representation of the
// names, with the values and the types given as parallel arrays.  Asserts if false.
void InterpreterTest::GlobalIsObject(const std::string& name, std::vector<std::string>& names,
                                     std::vector<llcpp::fuchsia::shell::Node*>& values,
                                     std::vector<llcpp::fuchsia::shell::ShellType>& types) {
  auto result = globals_.find(name);
  ASSERT_NE(result, globals_.end()) << "Global with name " << name << " not found";
  using Node = llcpp::fuchsia::shell::Node;
  fidl::VectorView<Node>& nodes = result->second->nodes;

  // Makes sure there is an object and a schema.
  Node* obj_root = nullptr;
  Node* schema_root = nullptr;
  for (Node& node : nodes) {
    if (node.is_object()) {
      obj_root = &node;
    }
    if (node.is_object_schema()) {
      schema_root = &node;
    }
  }
  ASSERT_NE(nullptr, obj_root) << "No object found for global " << name;
  ASSERT_NE(nullptr, schema_root) << "No object schema found for global " << name;

  size_t num_fields = names.size();
  // Sanity checking: should be guaranteed by the caller.
  ASSERT_EQ(num_fields, values.size());
  ASSERT_EQ(num_fields, types.size());

  // Figure out which name in the actual data goes with which type and which value.
  // Populate these maps with the information.
  std::map<const std::string, const llcpp::fuchsia::shell::ShellType*> names_to_types;
  std::map<const std::string, const Node*> names_to_values;

  const llcpp::fuchsia::shell::ObjectSchemaDefinition& object_schema = schema_root->object_schema();
  const fidl::VectorView<::llcpp::fuchsia::shell::NodeId>& field_schemas = object_schema.fields;
  ASSERT_EQ(num_fields, field_schemas.count());
  for (auto& id : field_schemas) {
    ASSERT_TRUE(id.node_id < nodes.count()) << "Schema node id too high!";
    // Node ids are always offset by one.
    Node* field_schema_node = &nodes.at(id.node_id - 1);
    ASSERT_TRUE(field_schema_node->is_field_schema())
        << "Declared field schema is not a field schema";
    const llcpp::fuchsia::shell::ObjectFieldSchemaDefinition& field_schema =
        field_schema_node->field_schema();
    names_to_types.emplace(std::make_pair(
        std::string(field_schema.name.data(), field_schema.name.size()), &field_schema.type));

    for (auto& field : obj_root->object().fields) {
      Node* obj_field_definition = &nodes.at(field.node_id - 1);
      ASSERT_TRUE(obj_field_definition->is_object_field()) << "Node given as field is not field";
      auto& obj_field_schema = obj_field_definition->object_field().object_field_schema;
      if (obj_field_schema.node_id != id.node_id) {
        continue;
      }
      names_to_values.emplace(std::string(field_schema.name.data(), field_schema.name.size()),
                              &nodes.at(obj_field_definition->object_field().value.node_id - 1));
      break;
    }
  }

  // Check that the information we gathered about the actual data matches the expected data.
  for (size_t i = 0; i < num_fields; i++) {
    const auto& def = names_to_types.find(names[i]);
    ASSERT_NE(names_to_types.end(), def) << "Definition for field " << names[i] << " not found";
    const auto& val = names_to_values.find(names[i]);
    ASSERT_NE(names_to_values.end(), val) << "Value for field " << names[i] << " not found";
    Node* expected = values[i];

    if (def->second->is_builtin_type()) {
      switch (def->second->builtin_type()) {
        case llcpp::fuchsia::shell::BuiltinType::UINT64:
          // Sanity check our input.
          ASSERT_TRUE(expected->is_integer_literal());
          NodeIsInteger(val->second, expected->integer_literal().absolute_value[0],
                        expected->integer_literal().negative);
          break;
          // TODO(jeremymanson): Other types.
        default:
          FAIL() << "Unexpected type in object field";
      }
    } else {
      FAIL() << "Unexpected type in object field";
    }
  }
}

void InterpreterTest::SetUp() {
  zx_handle_t client_ch;
  zx_handle_t server_ch;
  zx_channel_create(0, &client_ch, &server_ch);
  zx::channel client_channel(client_ch);
  shell_ = std::make_unique<llcpp::fuchsia::shell::Shell::SyncClient>(std::move(client_channel));

  // Reset context ids.
  last_context_id_ = 0;
  // Resets the global error stream for the test (to be able to run multiple tests).
  global_error_stream_.str() = "";

  zx::channel server_channel(server_ch);
  // Creates a new connection to the server.
  ASSERT_EQ(ZX_OK, shell_provider_->Connect("fuchsia.shell.Shell", std::move(server_channel)));
}
