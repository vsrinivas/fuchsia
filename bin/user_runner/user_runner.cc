// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <trace-provider/provider.h>

#include "lib/app/cpp/application_context.h"
#include "peridot/bin/user_runner/user_runner_impl.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fidl/cpp/bindings/interface_ptr.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/macros.h"
#include "lib/fsl/tasks/message_loop.h"

namespace modular {

// Implementation of the user runner app.
class UserRunnerApp {
 public:
  explicit UserRunnerApp(const bool test)
      : application_context_(app::ApplicationContext::CreateFromStartupInfo()),
        user_runner_impl_(application_context_, test) {
    application_context_->outgoing_services()->AddService<UserRunner>(
        [this](fidl::InterfaceRequest<UserRunner> request) {
          user_runner_impl_.Connect(std::move(request));
        });
  }

 private:
  std::shared_ptr<app::ApplicationContext> application_context_;
  UserRunnerImpl user_runner_impl_;
  FXL_DISALLOW_COPY_AND_ASSIGN(UserRunnerApp);
};

}  // namespace modular

int main(int argc, const char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  const bool test = command_line.HasOption("test");
  fsl::MessageLoop loop;
  trace::TraceProvider trace_provider(loop.async());
  modular::UserRunnerApp app(test);
  loop.Run();
  return 0;
}
