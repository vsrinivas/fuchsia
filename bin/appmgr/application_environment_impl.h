// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPLICATION_SRC_MANAGER_APPLICATION_ENVIRONMENT_IMPL_H_
#define APPLICATION_SRC_MANAGER_APPLICATION_ENVIRONMENT_IMPL_H_

#include <iosfwd>
#include <memory>
#include <string>
#include <unordered_map>

#include "lib/svc/cpp/service_provider_bridge.h"
#include "lib/app/fidl/application_environment.fidl.h"
#include "lib/app/fidl/application_loader.fidl.h"
#include "garnet/bin/appmgr/application_controller_impl.h"
#include "garnet/bin/appmgr/application_environment_controller_impl.h"
#include "garnet/bin/appmgr/application_runner_holder.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/strings/string_view.h"

namespace app {

class ApplicationEnvironmentImpl : public ApplicationEnvironment,
                                   public ApplicationLauncher {
 public:
  ApplicationEnvironmentImpl(
      ApplicationEnvironmentImpl* parent,
      fidl::InterfaceHandle<ApplicationEnvironmentHost> host,
      const fidl::String& label);
  ~ApplicationEnvironmentImpl() override;

  ApplicationEnvironmentImpl* parent() const { return parent_; }
  const std::string& label() const { return label_; }

  // Removes the child environment from this environment and returns the owning
  // reference to the child's controller. The caller of this function typically
  // destroys the controller (and hence the environment) shortly after calling
  // this function.
  std::unique_ptr<ApplicationEnvironmentControllerImpl> ExtractChild(
      ApplicationEnvironmentImpl* child);

  // Removes the application from this environment and returns the owning
  // reference to the application's controller. The caller of this function
  // typically destroys the controller (and hence the application) shortly after
  // calling this function.
  std::unique_ptr<ApplicationControllerImpl> ExtractApplication(
      ApplicationControllerImpl* controller);

  void AddBinding(fidl::InterfaceRequest<ApplicationEnvironment> environment);

  // ApplicationEnvironment implementation:

  void CreateNestedEnvironment(
      fidl::InterfaceHandle<ApplicationEnvironmentHost> host,
      fidl::InterfaceRequest<ApplicationEnvironment> environment,
      fidl::InterfaceRequest<ApplicationEnvironmentController> controller,
      const fidl::String& label) override;

  void GetApplicationLauncher(
      fidl::InterfaceRequest<ApplicationLauncher> launcher) override;

  void GetServices(fidl::InterfaceRequest<ServiceProvider> services) override;

  // ApplicationLauncher implementation:

  void CreateApplication(
      ApplicationLaunchInfoPtr launch_info,
      fidl::InterfaceRequest<ApplicationController> controller) override;

 private:
  static uint32_t next_numbered_label_;

  ApplicationRunnerHolder* GetOrCreateRunner(const std::string& runner);

  void CreateApplicationWithRunner(
      ApplicationPackagePtr package,
      ApplicationLaunchInfoPtr launch_info,
      std::string runner,
      fidl::InterfaceRequest<ApplicationController> controller);
  void CreateApplicationWithProcess(
      ApplicationPackagePtr package,
      ApplicationLaunchInfoPtr launch_info,
      fidl::InterfaceRequest<ApplicationController> controller);
  void CreateApplicationFromArchive(
      ApplicationPackagePtr package,
      ApplicationLaunchInfoPtr launch_info,
      fidl::InterfaceRequest<ApplicationController> controller);

  fidl::BindingSet<ApplicationEnvironment> environment_bindings_;
  fidl::BindingSet<ApplicationLauncher> launcher_bindings_;

  ServiceProviderBridge services_;

  ApplicationEnvironmentImpl* parent_;
  ApplicationEnvironmentHostPtr host_;
  ApplicationLoaderPtr loader_;
  std::string label_;

  mx::job job_;
  mx::job job_for_child_;

  std::unordered_map<ApplicationEnvironmentImpl*,
                     std::unique_ptr<ApplicationEnvironmentControllerImpl>>
      children_;

  std::unordered_map<ApplicationControllerImpl*,
                     std::unique_ptr<ApplicationControllerImpl>>
      applications_;

  std::unordered_map<std::string, std::unique_ptr<ApplicationRunnerHolder>>
      runners_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ApplicationEnvironmentImpl);
};

}  // namespace app

#endif  // APPLICATION_SRC_MANAGER_APPLICATION_ENVIRONMENT_IMPL_H_
