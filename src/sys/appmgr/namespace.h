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

#include "lib/fidl/cpp/binding_set.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/memory/ref_counted.h"
#include "src/lib/storage/vfs/cpp/managed_vfs.h"
#include "src/sys/appmgr/job_provider_impl.h"
#include "src/sys/appmgr/moniker.h"
#include "src/sys/appmgr/service_provider_dir_impl.h"

namespace component {
class Realm;

class Namespace : public fuchsia::sys::Environment,
                  public fuchsia::sys::Launcher,
                  public fxl::RefCountedThreadSafe<Namespace> {
 public:
  enum Status {
    /// Namespace running and serving request.
    RUNNING,

    /// Got shutdown request.
    SHUTTING_DOWN,

    /// Stopping vfs and dependencies.
    STOPPING,

    /// Namespace stopped.
    STOPPED,
  };
  const fbl::RefPtr<ServiceProviderDirImpl>& services() const { return services_; }
  const fbl::RefPtr<JobProviderImpl>& job_provider() { return job_provider_; }
  fxl::WeakPtr<Realm> realm() const { return realm_; }
  Status status() { return status_; }

  void AddBinding(fidl::InterfaceRequest<fuchsia::sys::Environment> environment);

  zx_status_t ServeServiceDirectory(fidl::InterfaceRequest<fuchsia::io::Directory> request);

  fidl::InterfaceHandle<fuchsia::io::Directory> OpenServicesAsDirectory();

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

  void GetDirectory(fidl::InterfaceRequest<fuchsia::io::Directory> directory_request) override {
    ServeServiceDirectory(std::move(directory_request));
  }

  void set_component_moniker(const Moniker& moniker) { services_->set_component_moniker(moniker); }
  void set_component_id(const std::string& id) { services_->set_component_id(id); }

  //
  // fuchsia::sys::Launcher implementation:
  //

  void CreateComponent(
      fuchsia::sys::LaunchInfo launch_info,
      fidl::InterfaceRequest<fuchsia::sys::ComponentController> controller) override;

  // Adds the service to the service directory if it's allowlisted.
  void MaybeAddComponentEventProvider();

  // Notifies a realms ComponentEventListener with the out/diagnostics directory for a component.
  void NotifyComponentDiagnosticsDirReady(const std::string& component_url,
                                          const std::string& component_name,
                                          const std::string& component_id,
                                          fidl::InterfaceHandle<fuchsia::io::Directory> directory);

  // Notifies a realms ComponentEventListener that a component started.
  void NotifyComponentStarted(const std::string& component_url, const std::string& component_name,
                              const std::string& component_id);

  // Notifies a realms ComponentEventListener that a component stopped.
  void NotifyComponentStopped(const std::string& component_url, const std::string& component_name,
                              const std::string& component_id);

  // Proccesses all pending messages and shuts downs children and self.
  // We handle shutdown here and not in realm and component as
  // 1. It is lot of work to get realm and component to maintain state and close all dependencies.
  // 2. Namespace doesn't need realm and component to be active so we can shut it down in
  // background.
  // 3. We anyways need to do this as a namespace might be dependent on a parent namespace, so
  // parent should make sure all if its child namespace shutdown before it does.
  void FlushAndShutdown(fxl::RefPtr<Namespace> self,
                        fs::ManagedVfs::CloseAllConnectionsForVnodeCallback callback = nullptr);

  // Create child namespace. Returns |null| if the namespace is shutting down.
  static fxl::RefPtr<Namespace> CreateChildNamespace(
      fxl::RefPtr<Namespace>& parent, fxl::WeakPtr<Realm> realm,
      fuchsia::sys::ServiceListPtr additional_services,
      const std::vector<std::string>* service_allowlist);

 private:
  // So that constructor with parent namesapace cannot be used directly.
  struct PrivateConstructor {};

  FRIEND_MAKE_REF_COUNTED(Namespace);
  Namespace(fxl::WeakPtr<Realm> realm, fuchsia::sys::ServiceListPtr additional_services,
            const std::vector<std::string>* service_allowlist);

  // Use CreateChildNamespace.
  Namespace(PrivateConstructor p, fxl::RefPtr<Namespace> parent, fxl::WeakPtr<Realm> realm,
            fuchsia::sys::ServiceListPtr additional_services,
            const std::vector<std::string>* service_allowlist);

  FRIEND_REF_COUNTED_THREAD_SAFE(Namespace);
  ~Namespace() override;

  void AddChild(fxl::RefPtr<Namespace> child);

  /// Removes child and returns true if the child was present.
  bool RemoveChild(Namespace* child);

  static void RunShutdownIfNoChildren(fxl::RefPtr<Namespace> ns);

  fidl::BindingSet<fuchsia::sys::Environment> environment_bindings_;
  fidl::BindingSet<fuchsia::sys::Launcher> launcher_bindings_;
  fidl::BindingSet<fuchsia::process::Resolver> resolver_bindings_;

  fs::ManagedVfs vfs_;
  fbl::RefPtr<ServiceProviderDirImpl> services_;
  fbl::RefPtr<JobProviderImpl> job_provider_;
  // realm_ can be null when it is shutting down and we kill namespace in background.
  fxl::WeakPtr<Realm> realm_;
  // Set if |additional_services.provider| was set.
  fuchsia::sys::ServiceProviderPtr service_provider_;
  // Set if |additional_services.host_directory| was set.
  fidl::InterfaceHandle<fuchsia::io::Directory> service_host_directory_;
  fuchsia::sys::LoaderPtr loader_;

  // Weak ptr to store in children namesapce.
  fxl::WeakPtrFactory<Namespace> weak_ptr_factory_;

  // List of children which have this namespace as its parent. Children should be shutdown before
  // this namespace is killed.
  std::map<Namespace*, fxl::RefPtr<Namespace>> children_;

  // Running status of this namespace.
  Status status_;

  // Callbacks to call when shutdown completes.
  std::vector<fs::ManagedVfs::CloseAllConnectionsForVnodeCallback> shutdown_callbacks_;

  // store parent reference
  fxl::WeakPtr<Namespace> parent_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Namespace);
};

}  // namespace component

#endif  // SRC_SYS_APPMGR_NAMESPACE_H_
