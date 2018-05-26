// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <auth/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <trace-provider/provider.h>

#include "garnet/bin/auth/token_manager/test/factory_impl.h"
#include "lib/app/cpp/application_context.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fidl/cpp/interface_request.h"
#include "lib/fsl/vmo/strings.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/log_settings_command_line.h"

namespace {

///////////////////////////////////////////////////////////
// class DevAuthProviderApp
///////////////////////////////////////////////////////////

class DevAuthProviderApp {
 public:
  DevAuthProviderApp()
      : loop_(&kAsyncLoopConfigMakeDefault),
        app_context_(component::ApplicationContext::CreateFromStartupInfo()),
        trace_provider_(loop_.async()) {
    FXL_CHECK(app_context_);
  }

  void Run() {
    app_context_->outgoing().AddPublicService<auth::AuthProviderFactory>(
        [this](fidl::InterfaceRequest<auth::AuthProviderFactory> request) {
          factory_bindings_.AddBinding(&factory_impl_, std::move(request));
        });
    loop_.Run();
  }

 private:
  async::Loop loop_;
  std::unique_ptr<component::ApplicationContext> app_context_;
  trace::TraceProvider trace_provider_;

  auth::dev_auth_provider::FactoryImpl factory_impl_;
  fidl::BindingSet<auth::AuthProviderFactory> factory_bindings_;

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
