// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_APPMGR_NAMESPACE_H_
#define SRC_SYS_APPMGR_NAMESPACE_H_

#include <fuchsia/process/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <fs/synchronous_vfs.h>

#include "lib/fidl/cpp/binding_set.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/memory/ref_counted.h"
#include "src/lib/fxl/strings/string_view.h"
#include "src/sys/appmgr/job_provider_impl.h"
#include "src/sys/appmgr/service_provider_dir_impl.h"

namespace component {
class Realm;

class Namespace : public fuchsia::sys::Environment,
                  public fuchsia::sys::Launcher,
                  public fuchsia::process::Resolver,
                  public fxl::RefCountedThreadSafe<Namespace> {
 public:
  const fbl::RefPtr<ServiceProviderDirImpl>& services() const { return services_; }
  const fbl::RefPtr<JobProviderImpl>& job_provider() { return job_provider_; }

  void AddBinding(fidl::InterfaceRequest<fuchsia::sys::Environment> environment);

  zx_status_t ServeServiceDirectory(zx::channel request);

  zx::channel OpenServicesAsDirectory();

  //
  // fuchsia::process::Resolver implementation:
  //
  void Resolve(std::string name, fuchsia::process::Resolver::ResolveCallback callback) override;

  //
  // fuchsia::sys::Environment implementation:
  //

  void CreateNestedEnvironment(
      fidl::InterfaceRequest<fuchsia::sys::Environment> environment,
      fidl::InterfaceRequest<fuchsia::sys::EnvironmentController> controller, std::string label,
      fuchsia::sys::ServiceListPtr additional_services,
      fuchsia::sys::EnvironmentOptions options) override;

  void GetLauncher(fidl::InterfaceRequest<fuchsia::sys::Launcher> launcher) override;

  void GetServices(fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> services) override;

  void GetDirectory(zx::channel directory_request) override {
    ServeServiceDirectory(std::move(directory_request));
  }

  void set_component_url(const std::string& url) { services_->set_component_url(url); }

  //
  // fuchsia::sys::Launcher implementation:
  //

  void CreateComponent(
      fuchsia::sys::LaunchInfo launch_info,
      fidl::InterfaceRequest<fuchsia::sys::ComponentController> controller) override;

  // Notifies a realms ComponentEventListener with the out/diagnostics directory for a component.
  void NotifyComponentDiagnosticsDirReady(const std::string& component_url,
                                          const std::string& component_name,
                                          const std::string& component_id,
                                          fidl::InterfaceHandle<fuchsia::io::Directory> directory);

 private:
  FRIEND_MAKE_REF_COUNTED(Namespace);
  Namespace(fxl::RefPtr<Namespace> parent, Realm* realm,
            fuchsia::sys::ServiceListPtr additional_services,
            const std::vector<std::string>* service_whitelist);

  FRIEND_REF_COUNTED_THREAD_SAFE(Namespace);
  ~Namespace() override;

  fidl::BindingSet<fuchsia::sys::Environment> environment_bindings_;
  fidl::BindingSet<fuchsia::sys::Launcher> launcher_bindings_;
  fidl::BindingSet<fuchsia::process::Resolver> resolver_bindings_;

  fs::SynchronousVfs vfs_;
  fbl::RefPtr<ServiceProviderDirImpl> services_;
  fbl::RefPtr<JobProviderImpl> job_provider_;
  Realm* const realm_;
  // Set if |additional_services.provider| was set.
  fuchsia::sys::ServiceProviderPtr service_provider_;
  // Set if |additional_services.host_directory| was set.
  zx::channel service_host_directory_;
  fuchsia::sys::LoaderPtr loader_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Namespace);
};

}  // namespace component

#endif  // SRC_SYS_APPMGR_NAMESPACE_H_
