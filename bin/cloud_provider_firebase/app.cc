// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <trace-provider/provider.h>

#include "lib/app/cpp/application_context.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/log_settings_command_line.h"
#include "lib/lifecycle/fidl/lifecycle.fidl.h"
#include "peridot/bin/cloud_provider_firebase/factory_impl.h"
#include "peridot/bin/ledger/network/network_service_impl.h"

namespace cloud_provider_firebase {
namespace {

class App : public modular::Lifecycle {
 public:
  App()
      : application_context_(app::ApplicationContext::CreateFromStartupInfo()),
        trace_provider_(loop_.async()),
        network_service_(
            loop_.task_runner(),
            [this] {
              return application_context_
                  ->ConnectToEnvironmentService<network::NetworkService>();
            }),
        factory_impl_(loop_.task_runner(), &network_service_) {
    FXL_DCHECK(application_context_);
  }

  void Run() {
    application_context_->outgoing_services()->AddService<modular::Lifecycle>(
        [this](fidl::InterfaceRequest<modular::Lifecycle> request) {
          lifecycle_bindings_.AddBinding(this, std::move(request));
        });
    application_context_->outgoing_services()->AddService<Factory>(
        [this](fidl::InterfaceRequest<Factory> request) {
          factory_bindings_.AddBinding(&factory_impl_, std::move(request));
        });
    loop_.Run();
  }

  void Terminate() override { loop_.PostQuitTask(); }

 private:
  fsl::MessageLoop loop_;
  std::unique_ptr<app::ApplicationContext> application_context_;
  trace::TraceProvider trace_provider_;

  ledger::NetworkServiceImpl network_service_;
  FactoryImpl factory_impl_;
  fidl::BindingSet<modular::Lifecycle> lifecycle_bindings_;
  fidl::BindingSet<Factory> factory_bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(App);
};
}  // namespace

}  // namespace cloud_provider_firebase

int main(int argc, const char** argv) {
  const auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  fxl::SetLogSettingsFromCommandLine(command_line);

  cloud_provider_firebase::App app;
  app.Run();

  return 0;
}
