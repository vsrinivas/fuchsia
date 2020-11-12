// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>

#include <iostream>

#include "test/placeholders/cpp/fidl.h"
#include "lib/async-loop/cpp/loop.h"
#include "lib/async-loop/default.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/sys/cpp/component_context.h"
#include "src/lib/files/glob.h"
#include "src/lib/fxl/command_line.h"

static constexpr char kCmdHelp[] = "help";
static constexpr char kCmdServeDelayed[] = "serve_out_delayed_50ms";

static constexpr char kUsage[] = R"(
  Usage: constructor_helper_proc [-e] [-k kill_string]

  Helper process to test ComponentContext's static constructors.

  Serves an Echo server to its outgoing directory and exits with code 0 after
  the first message is sent to the Echo server.

  Exits with code 1 if the incoming namespace does NOT contain "/dont_error".

Arguments:
  --help: Shows this help page and exits
  --serve_out_delayed_50ms: Serves the out/ dir in a delayed task posted to the run loop.
)";

class EchoServer : public test::placeholders::Echo {
 public:
  void EchoString(::fidl::StringPtr value, EchoStringCallback callback) override {
    std::string intercept = value.value_or("");
    callback(std::move(value));
    if (listener_) {
      listener_(std::move(intercept));
    }
  }

  fidl::InterfaceRequestHandler<test::placeholders::Echo> GetHandler() {
    return bindings_.GetHandler(this);
  }

  void SetListener(fit::function<void(std::string)> list) { listener_ = std::move(list); }

 private:
  fidl::BindingSet<test::placeholders::Echo> bindings_;
  fit::function<void(std::string)> listener_;
};

int main(int argc, const char** argv) {
  std::cout << "Hello from constructor helper proc." << std::endl;

  if (files::Glob("/dont_error").size() == 0) {
    return 1;
  }

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto cmdline = fxl::CommandLineFromArgcArgv(argc, argv);
  if (cmdline.HasOption(kCmdHelp)) {
    std::cout << kUsage;
    return 0;
  }
  bool delayed = false;
  if (cmdline.HasOption(kCmdServeDelayed)) {
    delayed = false;
  }

  auto echo_server = std::make_unique<EchoServer>();
  echo_server->SetListener([&loop](std::string str) { loop.Quit(); });

  std::unique_ptr<sys::ComponentContext> context;
  if (delayed) {
    context = sys::ComponentContext::Create();
    async::PostDelayedTask(
        async_get_default_dispatcher(),
        [context = context.get(), echo_server = echo_server.get()] {
          context->outgoing()->AddPublicService(echo_server->GetHandler());
          context->outgoing()->ServeFromStartupInfo();
        },
        zx::msec(50));
  } else {
    context = sys::ComponentContext::CreateAndServeOutgoingDirectory();
    context->outgoing()->AddPublicService(echo_server->GetHandler());
  }

  loop.Run();
  std::cout << "Goodbye from constructor helper proc" << std::endl;
  return 0;
}
