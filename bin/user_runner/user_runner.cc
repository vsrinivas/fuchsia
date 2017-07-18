// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "application/lib/app/application_context.h"
#include "apps/modular/src/user_runner/user_runner_impl.h"
#include "apps/tracing/lib/trace/provider.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fidl/cpp/bindings/interface_ptr.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/tasks/message_loop.h"

namespace modular {

// Implementation of the user runner app.
class UserRunnerApp {
 public:
  UserRunnerApp(const bool test)
      : application_context_(app::ApplicationContext::CreateFromStartupInfo()),
        user_runner_impl_(application_context_, test) {
    application_context_->outgoing_services()->AddService<UserRunner>(
        [this](fidl::InterfaceRequest<UserRunner> request) {
          user_runner_impl_.Connect(std::move(request));
        });
    tracing::InitializeTracer(application_context_.get(), {"user_runner"});
  }

 private:
  std::shared_ptr<app::ApplicationContext> application_context_;
  UserRunnerImpl user_runner_impl_;
  FTL_DISALLOW_COPY_AND_ASSIGN(UserRunnerApp);
};

}  // namespace modular

int main(int argc, const char** argv) {
  auto command_line = ftl::CommandLineFromArgcArgv(argc, argv);
  const bool test = command_line.HasOption("test");
  mtl::MessageLoop loop;
  modular::UserRunnerApp app(test);
  loop.Run();
  return 0;
}
