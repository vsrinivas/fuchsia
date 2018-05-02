// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_APPMGR_NAMESPACE_H_
#define GARNET_BIN_APPMGR_NAMESPACE_H_

#include <memory>
#include <string>
#include <unordered_map>

#include <fuchsia/cpp/component.h>
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/ref_counted.h"
#include "lib/fxl/strings/string_view.h"
#include "lib/svc/cpp/service_provider_bridge.h"

namespace component {
class JobHolder;

class Namespace : public ApplicationEnvironment,
                  public ApplicationLauncher,
                  public fxl::RefCountedThreadSafe<Namespace> {
 public:
  ServiceProviderBridge& services() { return services_; }

  void AddBinding(fidl::InterfaceRequest<ApplicationEnvironment> environment);

  // ApplicationEnvironment implementation:

  void CreateNestedEnvironment(
      zx::channel host_directory,
      fidl::InterfaceRequest<ApplicationEnvironment> environment,
      fidl::InterfaceRequest<ApplicationEnvironmentController> controller,
      fidl::StringPtr label) override;

  void GetApplicationLauncher(
      fidl::InterfaceRequest<ApplicationLauncher> launcher) override;

  void GetServices(fidl::InterfaceRequest<ServiceProvider> services) override;

  void GetDirectory(zx::channel directory_request) override;

  // ApplicationLauncher implementation:

  void CreateApplication(
      ApplicationLaunchInfo launch_info,
      fidl::InterfaceRequest<ApplicationController> controller) override;

 private:
  FRIEND_MAKE_REF_COUNTED(Namespace);
  Namespace(fxl::RefPtr<Namespace> parent, JobHolder* job_holder,
            ServiceListPtr service_list);

  FRIEND_REF_COUNTED_THREAD_SAFE(Namespace);
  ~Namespace() override;

  fidl::BindingSet<ApplicationEnvironment> environment_bindings_;
  fidl::BindingSet<ApplicationLauncher> launcher_bindings_;

  ServiceProviderBridge services_;

  fxl::RefPtr<Namespace> parent_;
  JobHolder* job_holder_;
  ServiceProviderPtr additional_services_;
  ApplicationLoaderPtr loader_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Namespace);
};

}  // namespace component

#endif  // GARNET_BIN_APPMGR_APPLICATION_NAMESPACE_H_
