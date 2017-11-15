// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <trace-provider/provider.h>
#include <memory>

#include "garnet/bin/auth/token_manager/test/factory_impl.h"
#include "lib/app/cpp/application_context.h"
#include "lib/auth/fidl/auth_provider_factory.fidl.h"
#include "lib/auth/fidl/token_manager.fidl.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/fsl/tasks/message_loop.h"
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
      : app_context_(app::ApplicationContext::CreateFromStartupInfo()),
        trace_provider_(loop_.async()) {
    FXL_DCHECK(app_context_);
  }

  void Run() {
    app_context_->outgoing_services()->AddService<auth::AuthProviderFactory>(
        [this](fidl::InterfaceRequest<auth::AuthProviderFactory> request) {
          factory_bindings_.AddBinding(&factory_impl_, std::move(request));
        });
    loop_.Run();
  }

 private:
  fsl::MessageLoop loop_;
  std::unique_ptr<app::ApplicationContext> app_context_;
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
