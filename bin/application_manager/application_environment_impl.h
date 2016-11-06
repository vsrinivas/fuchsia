// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_SRC_APPLICATION_MANAGER_APPLICATION_ENVIRONMENT_IMPL_H_
#define APPS_MODULAR_SRC_APPLICATION_MANAGER_APPLICATION_ENVIRONMENT_IMPL_H_

#include <memory>
#include <unordered_map>

#include "apps/modular/src/application_manager/application_controller_impl.h"
#include "apps/modular/src/application_manager/application_environment_controller_impl.h"
#include "apps/modular/src/application_manager/application_runner_holder.h"
#include "apps/modular/services/application/application_environment.fidl.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/ftl/macros.h"

namespace modular {

class ApplicationEnvironmentImpl : public ApplicationEnvironment,
                                   public ApplicationLauncher {
 public:
  ApplicationEnvironmentImpl(
      ApplicationEnvironmentImpl* parent,
      fidl::InterfaceHandle<ApplicationEnvironmentHost> host);
  ~ApplicationEnvironmentImpl() override;

  ApplicationEnvironmentImpl* parent() const { return parent_; }

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

  // ApplicationEnvironment implementation:

  void CreateNestedEnvironment(
      fidl::InterfaceHandle<ApplicationEnvironmentHost> host,
      fidl::InterfaceRequest<ApplicationEnvironment> environment,
      fidl::InterfaceRequest<ApplicationEnvironmentController> controller)
      override;

  void GetApplicationLauncher(
      fidl::InterfaceRequest<ApplicationLauncher> launcher) override;

  void Duplicate(
      fidl::InterfaceRequest<ApplicationEnvironment> environment) override;

  // ApplicationLauncher implementation:

  void CreateApplication(
      ApplicationLaunchInfoPtr launch_info,
      fidl::InterfaceRequest<ApplicationController> controller) override;

 private:
  void CreateApplicationWithProcess(
      const std::string& path,
      fidl::Array<fidl::String> arguments,
      fidl::InterfaceHandle<ServiceProvider> environment_services,
      fidl::InterfaceRequest<ServiceProvider> services,
      fidl::InterfaceRequest<ApplicationController> controller);

  fidl::BindingSet<ApplicationEnvironment> environment_bindings_;
  fidl::BindingSet<ApplicationLauncher> launcher_bindings_;

  ApplicationEnvironmentImpl* parent_;
  ApplicationEnvironmentHostPtr host_;

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

}  // namespace modular

#endif  // APPS_MODULAR_SRC_APPLICATION_MANAGER_APPLICATION_CONTROLLER_IMPL_H_
