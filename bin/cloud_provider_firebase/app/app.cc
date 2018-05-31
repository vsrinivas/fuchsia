// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <trace-provider/provider.h>

#include <fuchsia/modular/cpp/fidl.h>
#include "lib/app/cpp/startup_context.h"
#include "lib/backoff/exponential_backoff.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/log_settings_command_line.h"
#include "lib/network_wrapper/network_wrapper_impl.h"
#include "peridot/bin/cloud_provider_firebase/app/factory_impl.h"

namespace cloud_provider_firebase {
namespace {

class App : public fuchsia::modular::Lifecycle {
 public:
  App()
      : startup_context_(component::StartupContext::CreateFromStartupInfo()),
        trace_provider_(loop_.async()),
        network_wrapper_(
            loop_.async(), std::make_unique<backoff::ExponentialBackoff>(),
            [this] {
              return startup_context_
                  ->ConnectToEnvironmentService<network::NetworkService>();
            }),
        factory_impl_(loop_.async(), &network_wrapper_) {
    FXL_DCHECK(startup_context_);
  }

  void Run() {
    startup_context_->outgoing().AddPublicService<fuchsia::modular::Lifecycle>(
        [this](fidl::InterfaceRequest<fuchsia::modular::Lifecycle> request) {
          lifecycle_bindings_.AddBinding(this, std::move(request));
        });
    startup_context_->outgoing().AddPublicService<Factory>(
        [this](fidl::InterfaceRequest<Factory> request) {
          factory_bindings_.AddBinding(&factory_impl_, std::move(request));
        });
    loop_.Run();
  }

  void Terminate() override { loop_.PostQuitTask(); }

 private:
  fsl::MessageLoop loop_;
  std::unique_ptr<component::StartupContext> startup_context_;
  trace::TraceProvider trace_provider_;

  network_wrapper::NetworkWrapperImpl network_wrapper_;
  FactoryImpl factory_impl_;
  fidl::BindingSet<fuchsia::modular::Lifecycle> lifecycle_bindings_;
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
