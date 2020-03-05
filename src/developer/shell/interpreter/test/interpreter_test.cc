// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/shell/interpreter/test/interpreter_test.h"

#include <iostream>
#include <sstream>
#include <string>

#include "fuchsia/shell/cpp/fidl.h"
#include "fuchsia/sys/cpp/fidl.h"
#include "lib/async-loop/default.h"
#include "zircon/status.h"

fuchsia::shell::ExecuteResult InterpreterTestContext::GetResult() const {
  std::string string = error_stream.str();
  if (!string.empty()) {
    std::cout << string;
  }
  return result;
}

InterpreterTest::InterpreterTest()
    : loop_(&kAsyncLoopConfigAttachToCurrentThread), context_(sys::ComponentContext::Create()) {
  ::fidl::InterfaceHandle<fuchsia::io::Directory> directory;

  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = "fuchsia-pkg://fuchsia.com/shell_server#meta/shell_server.cmx";
  launch_info.directory_request = directory.NewRequest().TakeChannel();

  fuchsia::sys::LauncherPtr launcher;
  context_->svc()->Connect(launcher.NewRequest());
  launcher->CreateComponent(std::move(launch_info), controller_.NewRequest());

  shell_provider_ = std::make_unique<sys::ServiceDirectory>(std::move(directory));

  shell_.set_error_handler([this](zx_status_t status) {
    global_error_stream_ << "Shell server closed connection: " << zx_status_get_string(status)
                         << "\n";
    loop_.Quit();
    ASSERT_FALSE(running_) << "Unexpected server termination.";
  });

  shell_.events().OnError = [this](uint64_t context_id,
                                   std::vector<fuchsia::shell::Location> locations,
                                   std::string error_message) {
    if (context_id == 0) {
      global_error_stream_ << error_message << "\n";
      Quit();
    } else {
      InterpreterTestContext* context = GetContext(context_id);
      ASSERT_NE(nullptr, context);
      for (const auto& location : locations) {
        if (location.has_node_id()) {
          context->error_stream << "node " << location.node_id().file_id << ':'
                                << location.node_id().node_id << ' ';
        }
      }
      context->error_stream << error_message << "\n";
    }
  };

  shell_.events().OnDumpDone = [this](uint64_t context_id) {
    InterpreterTestContext* context = GetContext(context_id);
    ASSERT_NE(nullptr, context);
    Quit();
  };

  shell_.events().OnExecutionDone = [this](uint64_t context_id,
                                           fuchsia::shell::ExecuteResult result) {
    InterpreterTestContext* context = GetContext(context_id);
    ASSERT_NE(nullptr, context);
    context->result = result;
    if (globals_to_load_.empty() || (result != fuchsia::shell::ExecuteResult::OK)) {
      Quit();
    } else {
      // Now that the execution is finished, loads all the global variables we asked using
      // LoadGlobal.
      for (const auto& global : globals_to_load_) {
        ++pending_globals_;
        shell()->LoadGlobal(global, [this, global](std::vector<fuchsia::shell::Node> nodes) {
          if (!nodes.empty()) {
            // Currently we only support single values.
            ASSERT_EQ(nodes.size(), static_cast<size_t>(1));
            globals_[global] = std::move(nodes[0]);
          }
          if (--pending_globals_ == 0) {
            Quit();
          }
        });
      }
    }
  };

  shell_.events().OnTextResult = [this](uint64_t context_id, std::string result,
                                        bool partial_result) {
    InterpreterTestContext* context = GetContext(context_id);
    ASSERT_NE(nullptr, context);
    if (last_result_partial_) {
      ASSERT_FALSE(results_.empty());
      results_.back() += result;
    } else {
      results_.emplace_back(std::move(result));
    }
    last_result_partial_ = partial_result;
  };
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

void InterpreterTest::SetUp() {
  // Reset context ids.
  last_context_id_ = 0;
  // Resets the global error stream for the test (to be able to run multiple tests).
  global_error_stream_.str() = "";
  // Creates a new connection to the server.
  shell_provider_->Connect(shell_.NewRequest());
}

fuchsia::shell::NodeId NullNode{0, 0};

fuchsia::shell::ShellType TypeUndef() {
  fuchsia::shell::ShellType type;
  type.set_undef(true);
  return type;
}

fuchsia::shell::ShellType TypeBool() {
  fuchsia::shell::ShellType type;
  type.set_builtin_type(fuchsia::shell::BuiltinType::BOOL);
  return type;
}

fuchsia::shell::ShellType TypeChar() {
  fuchsia::shell::ShellType type;
  type.set_builtin_type(fuchsia::shell::BuiltinType::CHAR);
  return type;
}

fuchsia::shell::ShellType TypeString() {
  fuchsia::shell::ShellType type;
  type.set_builtin_type(fuchsia::shell::BuiltinType::STRING);
  return type;
}

fuchsia::shell::ShellType TypeInt8() {
  fuchsia::shell::ShellType type;
  type.set_builtin_type(fuchsia::shell::BuiltinType::INT8);
  return type;
}

fuchsia::shell::ShellType TypeUint8() {
  fuchsia::shell::ShellType type;
  type.set_builtin_type(fuchsia::shell::BuiltinType::UINT8);
  return type;
}

fuchsia::shell::ShellType TypeInt16() {
  fuchsia::shell::ShellType type;
  type.set_builtin_type(fuchsia::shell::BuiltinType::INT16);
  return type;
}

fuchsia::shell::ShellType TypeUint16() {
  fuchsia::shell::ShellType type;
  type.set_builtin_type(fuchsia::shell::BuiltinType::UINT16);
  return type;
}

fuchsia::shell::ShellType TypeInt32() {
  fuchsia::shell::ShellType type;
  type.set_builtin_type(fuchsia::shell::BuiltinType::INT32);
  return type;
}

fuchsia::shell::ShellType TypeUint32() {
  fuchsia::shell::ShellType type;
  type.set_builtin_type(fuchsia::shell::BuiltinType::UINT32);
  return type;
}

fuchsia::shell::ShellType TypeInt64() {
  fuchsia::shell::ShellType type;
  type.set_builtin_type(fuchsia::shell::BuiltinType::INT64);
  return type;
}

fuchsia::shell::ShellType TypeUint64() {
  fuchsia::shell::ShellType type;
  type.set_builtin_type(fuchsia::shell::BuiltinType::UINT64);
  return type;
}

fuchsia::shell::ShellType TypeInteger() {
  fuchsia::shell::ShellType type;
  type.set_builtin_type(fuchsia::shell::BuiltinType::INTEGER);
  return type;
}

fuchsia::shell::ShellType TypeFloat32() {
  fuchsia::shell::ShellType type;
  type.set_builtin_type(fuchsia::shell::BuiltinType::FLOAT32);
  return type;
}

fuchsia::shell::ShellType TypeFloat64() {
  fuchsia::shell::ShellType type;
  type.set_builtin_type(fuchsia::shell::BuiltinType::FLOAT64);
  return type;
}
