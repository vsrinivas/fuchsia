// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_APPMGR_NAMESPACE_H_
#define GARNET_BIN_APPMGR_NAMESPACE_H_

#include <memory>
#include <string>
#include <unordered_map>

#include <component/cpp/fidl.h>
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/ref_counted.h"
#include "lib/fxl/strings/string_view.h"
#include "lib/svc/cpp/service_provider_bridge.h"

namespace component {
class Realm;

class Namespace : public Environment,
                  public ApplicationLauncher,
                  public fxl::RefCountedThreadSafe<Namespace> {
 public:
  ServiceProviderBridge& services() { return services_; }

  void AddBinding(fidl::InterfaceRequest<Environment> environment);

  // Environment implementation:

  void CreateNestedEnvironment(
      zx::channel host_directory,
      fidl::InterfaceRequest<Environment> environment,
      fidl::InterfaceRequest<EnvironmentController> controller,
      fidl::StringPtr label) override;

  void GetApplicationLauncher(
      fidl::InterfaceRequest<ApplicationLauncher> launcher) override;

  void GetServices(fidl::InterfaceRequest<ServiceProvider> services) override;

  void GetDirectory(zx::channel directory_request) override;

  // ApplicationLauncher implementation:

  void CreateApplication(
      LaunchInfo launch_info,
      fidl::InterfaceRequest<ComponentController> controller) override;

 private:
  FRIEND_MAKE_REF_COUNTED(Namespace);
  Namespace(fxl::RefPtr<Namespace> parent, Realm* realm,
            ServiceListPtr service_list);

  FRIEND_REF_COUNTED_THREAD_SAFE(Namespace);
  ~Namespace() override;

  fidl::BindingSet<Environment> environment_bindings_;
  fidl::BindingSet<ApplicationLauncher> launcher_bindings_;

  ServiceProviderBridge services_;

  fxl::RefPtr<Namespace> parent_;
  Realm* realm_;
  ServiceProviderPtr additional_services_;
  LoaderPtr loader_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Namespace);
};

}  // namespace component

#endif  // GARNET_BIN_APPMGR_NAMESPACE_H_
