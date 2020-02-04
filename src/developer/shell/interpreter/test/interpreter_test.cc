// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/shell/interpreter/test/interpreter_test.h"

#include <sstream>
#include <string>

#include "fuchsia/shell/cpp/fidl.h"
#include "fuchsia/sys/cpp/fidl.h"
#include "lib/async-loop/default.h"
#include "zircon/status.h"

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
    Quit();
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
      // Currently we can only have global errors (not relative to a node).
      ASSERT_EQ(true, locations.empty());
      context->error_stream << error_message << "\n";
    }
  };

  shell_.events().OnExecutionDone = [this](uint64_t context_id,
                                           fuchsia::shell::ExecuteResult result) {
    InterpreterTestContext* context = GetContext(context_id);
    ASSERT_NE(nullptr, context);
    context->result = result;
    Quit();
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
