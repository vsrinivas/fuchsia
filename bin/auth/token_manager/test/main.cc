// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <fuchsia/auth/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <trace-provider/provider.h>

#include "garnet/bin/auth/token_manager/test/factory_impl.h"
#include "lib/component/cpp/startup_context.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fidl/cpp/interface_request.h"
#include "lib/fsl/vmo/strings.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/log_settings_command_line.h"

namespace {

using fuchsia::auth::AuthProviderFactory;

///////////////////////////////////////////////////////////
// class DevAuthProviderApp
///////////////////////////////////////////////////////////

class DevAuthProviderApp {
 public:
  DevAuthProviderApp()
      : loop_(&kAsyncLoopConfigAttachToThread),
        app_context_(component::StartupContext::CreateFromStartupInfo()),
        trace_provider_(loop_.dispatcher()) {
    FXL_CHECK(app_context_);
  }

  void Run() {
    app_context_->outgoing().AddPublicService(
        factory_bindings_.GetHandler(&factory_impl_));
    loop_.Run();
  }

 private:
  async::Loop loop_;
  std::unique_ptr<component::StartupContext> app_context_;
  trace::TraceProvider trace_provider_;

  auth::dev_auth_provider::FactoryImpl factory_impl_;
  fidl::BindingSet<AuthProviderFactory> factory_bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DevAuthProviderApp);
};

}  // namespace

int main(int argc, const char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(command_line)) {
    return 1;
  }

  DevAuthProviderApp app;
  app.Run();
  return 0;
}
