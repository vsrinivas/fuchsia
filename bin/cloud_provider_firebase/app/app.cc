// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <trace-provider/provider.h>

#include "garnet/lib/backoff/exponential_backoff.h"
#include "lib/app/cpp/application_context.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/log_settings_command_line.h"
#include "lib/lifecycle/fidl/lifecycle.fidl.h"
#include "peridot/bin/cloud_provider_firebase/app/factory_impl.h"
#include "garnet/lib/network_wrapper/network_wrapper_impl.h"

namespace cloud_provider_firebase {
namespace {

class App : public modular::Lifecycle {
 public:
  App()
      : application_context_(
            component::ApplicationContext::CreateFromStartupInfo()),
        trace_provider_(loop_.async()),
        network_wrapper_(
            loop_.task_runner(),
            std::make_unique<backoff::ExponentialBackoff>(),
            [this] {
              return application_context_
                  ->ConnectToEnvironmentService<network::NetworkService>();
            }),
        factory_impl_(loop_.task_runner(), &network_wrapper_) {
    FXL_DCHECK(application_context_);
  }

  void Run() {
    application_context_->outgoing_services()->AddService<modular::Lifecycle>(
        [this](f1dl::InterfaceRequest<modular::Lifecycle> request) {
          lifecycle_bindings_.AddBinding(this, std::move(request));
        });
    application_context_->outgoing_services()->AddService<Factory>(
        [this](f1dl::InterfaceRequest<Factory> request) {
          factory_bindings_.AddBinding(&factory_impl_, std::move(request));
        });
    loop_.Run();
  }

  void Terminate() override { loop_.PostQuitTask(); }

 private:
  fsl::MessageLoop loop_;
  std::unique_ptr<component::ApplicationContext> application_context_;
  trace::TraceProvider trace_provider_;

  network_wrapper::NetworkWrapperImpl network_wrapper_;
  FactoryImpl factory_impl_;
  f1dl::BindingSet<modular::Lifecycle> lifecycle_bindings_;
  f1dl::BindingSet<Factory> factory_bindings_;

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
