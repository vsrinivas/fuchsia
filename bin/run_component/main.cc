// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include "application/lib/app/application_context.h"
#include "application/lib/app/service_provider_impl.h"
#include "apps/modular/src/run_component/application_loader_impl.h"
#include "lib/ftl/command_line.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/tasks/message_loop.h"

namespace component {

class RunComponentApp : public modular::ApplicationEnvironmentHost {
 public:
  RunComponentApp(const ftl::CommandLine& command_line)
      : context_(modular::ApplicationContext::CreateFromStartupInfo()),
        application_loader_(
            context_->ConnectToEnvironmentService<ComponentIndex>()),
        env_host_binding_(this) {
    // TODO(ianloic): nicer command-line handling
    if (command_line.options().size() != 0 ||
        command_line.positional_args().size() != 1) {
      PrintUsage(command_line.argv0());
      exit(1);
    }
    const std::string& component_id = command_line.positional_args()[0];

    // Set up environment for the components we will run.
    modular::ApplicationEnvironmentHostPtr env_host;
    env_host_binding_.Bind(env_host.NewRequest());
    context_->environment()->CreateNestedEnvironment(
        std::move(env_host), env_.NewRequest(), env_controller_.NewRequest(),
        "run_component");
    env_->GetApplicationLauncher(env_launcher_.NewRequest());

    // Make the component application loader available in the new environment.
    env_services_.AddService<modular::ApplicationLoader>(
        [this](fidl::InterfaceRequest<modular::ApplicationLoader> request) {
          application_loader_bindings_.AddBinding(&application_loader_,
                                                  std::move(request));
        });

    // Make services from this environment available in the new environment.
    env_services_.SetDefaultServiceConnector(
        [this](std::string service_name, mx::channel channel) {
          context_->environment_services()->ConnectToService(
              service_name, std::move(channel));
        });

    FTL_LOG(INFO) << "Running component " << component_id;

    modular::ServiceProviderPtr app_services;

    auto launch_info = modular::ApplicationLaunchInfo::New();
    launch_info->url = component_id;
    launch_info->services = app_services.NewRequest();
    // TODO(ianloic): support passing arguments to component apps?

    modular::ApplicationControllerPtr controller;
    env_launcher_->CreateApplication(std::move(launch_info),
                                     controller.NewRequest());
    controller.set_connection_error_handler([] {
      FTL_LOG(INFO) << "Component terminated.";
      exit(0);
    });

    context_->outgoing_services()->SetDefaultServiceProvider(
        std::move(app_services));
  }

  void GetApplicationEnvironmentServices(
      ::fidl::InterfaceRequest<modular::ServiceProvider> environment_services)
      override {
    env_services_.AddBinding(std::move(environment_services));
  }

 private:
  void PrintUsage(const std::string argv0) {
    std::cout << "Usage: " << argv0 << " url_of_component_to_run" << std::endl;
  }

  std::unique_ptr<modular::ApplicationContext> context_;
  ApplicationLoaderImpl application_loader_;
  fidl::BindingSet<modular::ApplicationLoader> application_loader_bindings_;

  // Nested environment within which the components will run.
  modular::ApplicationEnvironmentPtr env_;
  modular::ApplicationEnvironmentControllerPtr env_controller_;
  fidl::Binding<modular::ApplicationEnvironmentHost> env_host_binding_;
  modular::ServiceProviderImpl env_services_;
  modular::ApplicationLauncherPtr env_launcher_;
};

}  // namespace component

int main(int argc, const char** argv) {
  auto command_line = ftl::CommandLineFromArgcArgv(argc, argv);

  mtl::MessageLoop loop;
  component::RunComponentApp app(command_line);
  loop.Run();

  return 0;
}
