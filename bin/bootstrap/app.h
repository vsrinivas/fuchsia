// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_SRC_BOOTSTRAP_APP_H_
#define APPS_MODULAR_SRC_BOOTSTRAP_APP_H_

#include <memory>
#include <vector>
#include <unordered_map>

#include "apps/modular/lib/app/application_context.h"
#include "apps/modular/lib/app/service_provider_impl.h"
#include "apps/modular/services/application/application_controller.fidl.h"
#include "apps/modular/services/application/application_environment.fidl.h"
#include "apps/mozart/services/views/view_manager.fidl.h"
#include "lib/ftl/command_line.h"
#include "lib/ftl/macros.h"

namespace bootstrap {

class Params;

// The bootstrap creates a nested environment within which it starts apps
// and wires up the UI services they require.
//
// The nested environment consists of the following system applications
// which are started on demand then retained as singletons for the lifetime
// of the environment.
//
// After setting up the nested environment, the bootstrap starts the app
// specified on the command-line.
class App : public modular::ApplicationEnvironmentHost {
 public:
  explicit App(Params* params);
  ~App();

 private:
  // |ApplicationEnvironmentHost|:
  void GetApplicationEnvironmentServices(
      fidl::InterfaceRequest<modular::ServiceProvider> environment_services)
      override;

  void RegisterSingleton(std::string service_name,
                         modular::ApplicationLaunchInfoPtr launch_info);
  void RegisterDefaultServiceConnector();
  void LaunchApplication(modular::ApplicationLaunchInfoPtr launch_info);

  void RegisterViewManager();
  void InitViewManager();
  void ResetViewManager();

  std::unique_ptr<modular::ApplicationContext> application_context_;

  // Keep track of all services, indexed by url.
  std::unordered_map<std::string, modular::ServiceProviderPtr>
      service_providers_;

  // Nested environment within which the apps started by Bootstrap will run.
  modular::ApplicationEnvironmentPtr env_;
  modular::ApplicationEnvironmentControllerPtr env_controller_;
  fidl::Binding<modular::ApplicationEnvironmentHost> env_host_binding_;
  modular::ServiceProviderImpl env_services_;
  modular::ApplicationLauncherPtr env_launcher_;

  // View manager state.
  mozart::ViewManagerPtr view_manager_;
  modular::ApplicationControllerPtr view_manager_controller_;
  modular::ServiceProviderPtr view_manager_services_;
  std::vector<modular::ApplicationControllerPtr> view_associate_controllers_;
  std::vector<mozart::ViewAssociateOwnerPtr> view_associate_owners_;

  FTL_DISALLOW_COPY_AND_ASSIGN(App);
};

}  // namespace bootstrap

#endif  // APPS_MODULAR_SRC_BOOTSTRAP_APP_H_
