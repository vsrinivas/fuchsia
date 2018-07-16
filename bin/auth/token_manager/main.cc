// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <fuchsia/auth/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <trace-provider/provider.h>

#include "garnet/bin/auth/token_manager/token_manager_factory_impl.h"
#include "lib/component/cpp/startup_context.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fidl/cpp/interface_request.h"
#include "lib/fsl/vmo/strings.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/log_settings_command_line.h"

namespace {

///////////////////////////////////////////////////////////
// class TokenManagerApp
///////////////////////////////////////////////////////////

class TokenManagerApp {
 public:
  TokenManagerApp(std::unique_ptr<component::StartupContext> context)
      : app_context_(std::move(context)), factory_impl_(app_context_.get()) {
    app_context_->outgoing().AddPublicService(
        factory_bindings_.GetHandler(&factory_impl_));
  }

 private:
  std::unique_ptr<component::StartupContext> app_context_;

  auth::TokenManagerFactoryImpl factory_impl_;
  fidl::BindingSet<auth::TokenManagerFactory> factory_bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TokenManagerApp);
};

}  // namespace

int main(int argc, const char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(command_line)) {
    return 1;
  }

  async::Loop loop(&kAsyncLoopConfigMakeDefault);
  trace::TraceProvider trace_provider(loop.dispatcher());
  TokenManagerApp app(component::StartupContext::CreateFromStartupInfo());
  loop.Run();
  return 0;
}
