// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_SRC_LAUNCHER_LAUNCHER_APP_H_
#define APPS_MOZART_SRC_LAUNCHER_LAUNCHER_APP_H_

#include <memory>
#include <unordered_map>
#include <vector>

#include "apps/modular/lib/app/application_context.h"
#include "apps/modular/lib/app/service_provider_impl.h"
#include "apps/modular/services/application/application_controller.fidl.h"
#include "apps/modular/services/application/application_environment.fidl.h"
#include "apps/mozart/services/launcher/launcher.fidl.h"
#include "apps/mozart/src/launcher/launch_instance.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/ftl/command_line.h"
#include "lib/ftl/macros.h"

namespace launcher {

// The launcher creates a nested environment within which it starts apps
// and wires up the UI services they require.
//
// The nested environment consists of the following system applications
// which are started on demand then retained as singletons for the lifetime
// of the environment.
//
//   - compositor: provides the |mozart::Compositor| service
//   - view_manager: provides the |mozart::ViewManager| service
//   - input_manager: registered as a view associate with the view manager
//   - fonts: provides the |fonts::FontProvider| service
//   - network: provides the |network::NetworkService| service
//
// After setting up the nested environment, the launcher starts the app
// specified on the command-line.
//
// If the app implements |mozart::ViewProvider| then the launcher asks it
// to create a view which is displayed as the root of a new view tree.
// It's ok if the app doesn't implement |mozart::ViewProvider|; it just
// means the launcher will not display any UI until asked.
//
// The launcher also exposes a |mozart::Launcher| service which apps running
// within the nested environment can use to ask it to display a view as
// the root of a new view tree.
//
// Any number of view trees can be created, although multi-display support
// and input routing is not fully supported (TODO).
class LauncherApp : public modular::ApplicationEnvironmentHost,
                    public mozart::Launcher {
 public:
  explicit LauncherApp(const ftl::CommandLine& command_line);
  ~LauncherApp();

 private:
  // |ApplicationEnvironmentHost|:
  void GetApplicationEnvironmentServices(
      fidl::InterfaceRequest<modular::ServiceProvider> environment_services)
      override;

  // |Launcher|:
  void Display(fidl::InterfaceHandle<mozart::ViewOwner> view_owner) override;

  void RegisterServices();
  void RegisterSingletonService(std::string service_name, std::string url);
  void InitCompositor();
  void InitViewManager();

  void Launch(fidl::String url, fidl::Array<fidl::String> arguments);
  void DisplayInternal(fidl::InterfaceHandle<mozart::ViewOwner> view_owner,
                       modular::ApplicationControllerPtr controller);

  void OnLaunchTermination(uint32_t id);

  std::unique_ptr<modular::ApplicationContext> application_context_;

  // Nested environment within which the apps started by the Launcher will run.
  modular::ApplicationEnvironmentPtr env_;
  modular::ApplicationEnvironmentControllerPtr env_controller_;
  fidl::Binding<modular::ApplicationEnvironmentHost> env_host_binding_;
  modular::ServiceProviderImpl env_services_;
  modular::ApplicationLauncherPtr env_launcher_;

  mozart::CompositorPtr compositor_;
  modular::ServiceProviderPtr compositor_services_;

  mozart::ViewManagerPtr view_manager_;
  modular::ServiceProviderPtr view_manager_services_;

  std::vector<std::string> view_associate_urls_;
  std::vector<mozart::ViewAssociateOwnerPtr> view_associate_owners_;

  fidl::BindingSet<Launcher> launcher_bindings_;
  std::unordered_map<uint32_t, std::unique_ptr<LaunchInstance>>
      launch_instances_;

  uint32_t next_id_ = 0u;

  FTL_DISALLOW_COPY_AND_ASSIGN(LauncherApp);
};

}  // namespace launcher

#endif  // APPS_MOZART_SRC_LAUNCHER_LAUNCHER_APP_H_
