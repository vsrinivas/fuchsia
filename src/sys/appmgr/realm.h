// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_APPMGR_REALM_H_
#define SRC_SYS_APPMGR_REALM_H_

#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/sys/internal/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/zx/channel.h>
#include <lib/zx/status.h>
#include <zircon/syscalls/policy.h>
#include <zircon/types.h>

#include <iosfwd>
#include <memory>
#include <string>
#include <unordered_map>

#include <fs/synchronous_vfs.h>

#include "garnet/lib/loader/package_loader.h"
#include "src/lib/cmx/runtime.h"
#include "src/lib/files/unique_fd.h"
#include "src/lib/fsl/vmo/file.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/memory/ref_ptr.h"
#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/sys/appmgr/cache_control.h"
#include "src/sys/appmgr/component_container.h"
#include "src/sys/appmgr/component_controller_impl.h"
#include "src/sys/appmgr/component_event_provider_impl.h"
#include "src/sys/appmgr/component_id_index.h"
#include "src/sys/appmgr/cpu_watcher.h"
#include "src/sys/appmgr/crash_introspector.h"
#include "src/sys/appmgr/environment_controller_impl.h"
#include "src/sys/appmgr/hub/hub_info.h"
#include "src/sys/appmgr/hub/realm_hub.h"
#include "src/sys/appmgr/log_connector_impl.h"
#include "src/sys/appmgr/moniker.h"
#include "src/sys/appmgr/namespace.h"
#include "src/sys/appmgr/namespace_builder.h"
#include "src/sys/appmgr/runner_holder.h"
#include "src/sys/appmgr/scheme_map.h"

namespace component {
class ComponentEventProviderImpl;

namespace internal {

constexpr char kRootLabel[] = "app";

// When a component event will be triggered, this struct will contain what provider to notify and
// with which component identity data.
struct EventNotificationInfo {
  ComponentEventProviderImpl* provider;
  fuchsia::sys::internal::SourceIdentity component;
};

enum class StorageType { DATA, CACHE, TEMP };

}  // namespace internal

struct RealmArgs {
  static RealmArgs Make(fxl::WeakPtr<Realm> parent, std::string label, std::string data_path,
                        std::string cache_path, std::string temp_path,
                        const std::shared_ptr<sys::ServiceDirectory>& env_services,
                        bool run_virtual_console, fuchsia::sys::EnvironmentOptions options,
                        fxl::UniqueFD appmgr_config_dir,
                        fbl::RefPtr<ComponentIdIndex> component_id_index);

  static RealmArgs MakeWithAdditionalServices(
      fxl::WeakPtr<Realm> parent, std::string label, std::string data_path, std::string cache_path,
      std::string temp_path, const std::shared_ptr<sys::ServiceDirectory>& env_services,
      bool run_virtual_console, fuchsia::sys::ServiceListPtr additional_services,
      fuchsia::sys::EnvironmentOptions options, fxl::UniqueFD appmgr_config_dir,
      fbl::RefPtr<ComponentIdIndex> component_id_index);

  static RealmArgs MakeWithCustomLoader(
      fxl::WeakPtr<Realm> parent, std::string label, std::string data_path, std::string cache_path,
      std::string temp_path, const std::shared_ptr<sys::ServiceDirectory>& env_services,
      bool run_virtual_console, fuchsia::sys::ServiceListPtr additional_services,
      fuchsia::sys::EnvironmentOptions options, fxl::UniqueFD appmgr_config_dir,
      fbl::RefPtr<ComponentIdIndex> component_id_index, fuchsia::sys::LoaderPtr loader);

  fxl::WeakPtr<Realm> parent;
  std::string label;
  std::string data_path;
  std::string cache_path;
  std::string temp_path;
  std::shared_ptr<sys::ServiceDirectory> environment_services;
  bool run_virtual_console;
  fuchsia::sys::ServiceListPtr additional_services;
  fuchsia::sys::EnvironmentOptions options;
  fxl::UniqueFD appmgr_config_dir;
  CpuWatcher* cpu_watcher;
  fbl::RefPtr<ComponentIdIndex> component_id_index;
  std::optional<fuchsia::sys::LoaderPtr> loader;
};

class Realm : public ComponentContainer<ComponentControllerImpl> {
 public:
  using ShutdownNamespaceCallback = fit::callback<void()>;
  static std::unique_ptr<Realm> Create(RealmArgs args);

  // Constructor to create a Realm object. Clients should call |Create|.
  Realm(RealmArgs args, zx::job job);

  ~Realm();

  fxl::WeakPtr<Realm> parent() const { return parent_; }
  CpuWatcher* cpu_watcher() const { return cpu_watcher_; }
  const std::string& label() const { return label_; }
  const std::string& data_path() const { return data_path_; }
  const std::string& cache_path() const { return cache_path_; }
  const std::string& temp_path() const { return temp_path_; }
  const std::string& koid() const { return koid_; }
  const fbl::RefPtr<LogConnectorImpl>& log_connector() const { return log_connector_; }

  const fbl::RefPtr<fs::PseudoDir>& hub_dir() const { return hub_.dir(); }

  std::shared_ptr<sys::ServiceDirectory> environment_services() const {
    return environment_services_;
  }

  HubInfo HubInfo();

  zx::job DuplicateJobForHub() const;

  const zx::job& job() const { return job_; }
  const std::unordered_map<ComponentControllerImpl*, std::shared_ptr<ComponentControllerImpl>>&
  applications() const {
    return applications_;
  }

  const std::unordered_map<std::string, std::unique_ptr<RunnerHolder>>& runners() const {
    return runners_;
  }

  const std::unordered_map<Realm*, std::unique_ptr<EnvironmentControllerImpl>>& children() const {
    return children_;
  }

  fxl::WeakPtr<Realm> weak_ptr() { return weak_ptr_factory_.GetWeakPtr(); }

  void CreateNestedEnvironment(
      fidl::InterfaceRequest<fuchsia::sys::Environment> environment,
      fidl::InterfaceRequest<fuchsia::sys::EnvironmentController> controller_request,
      std::string label, fuchsia::sys::ServiceListPtr additional_services,
      fuchsia::sys::EnvironmentOptions options);

  using ComponentObjectCreatedCallback =
      fit::function<void(std::weak_ptr<ComponentControllerImpl> component)>;

  void CreateComponent(fuchsia::sys::LaunchInfo launch_info,
                       fidl::InterfaceRequest<fuchsia::sys::ComponentController> controller,
                       ComponentObjectCreatedCallback callback = nullptr);

  // Removes the child realm from this realm and returns the owning
  // reference to the child's controller. The caller of this function
  // typically destroys the controller (and hence the environment) shortly
  // after calling this function.
  std::unique_ptr<EnvironmentControllerImpl> ExtractChild(Realm* child);

  // Removes the application from this environment and returns the owning
  // reference to the application's controller. The caller of this function
  // typically destroys the controller (and hence the application) shortly after
  // calling this function.
  // We use shared_ptr so that we can pass weak_ptrs to dependent code.
  std::shared_ptr<ComponentControllerImpl> ExtractComponent(
      ComponentControllerImpl* controller) override;

  void AddBinding(fidl::InterfaceRequest<fuchsia::sys::Environment> environment);

  // Binds the given channel to the services directory (/svc) for the very first
  // nested realm created. This function is only supported for the root realm,
  // otherwise it will do nothing and return ZX_ERR_NOT_SUPPORTED.
  zx_status_t BindFirstNestedRealmSvc(zx::channel channel);

  void CreateShell(const std::string& path, zx::channel svc);

  void Resolve(fidl::StringPtr name, fuchsia::process::Resolver::ResolveCallback callback);

  // Notifies the |ComponentEventListener| of this realm or the closest parent realm (if there's
  // one) with a component out/diagnostics directory when the directory is available.
  void NotifyComponentDiagnosticsDirReady(const std::string& component_url,
                                          const std::string& component_name,
                                          const std::string& instance_id,
                                          fidl::InterfaceHandle<fuchsia::io::Directory> directory);

  // Notifies the Realm components event subscriber when a component starts.
  void NotifyComponentStarted(const std::string& component_url, const std::string& component_name,
                              const std::string& instance_id);

  // Notifies the Realm components event subscriber when a component stops.
  void NotifyComponentStopped(const std::string& component_url, const std::string& component_name,
                              const std::string& instance_id);

  // Creates a connection to |fuchsia::sys::internal::ComponentEventProvider|.
  zx_status_t BindComponentEventProvider(
      fidl::InterfaceRequest<fuchsia::sys::internal::ComponentEventProvider> request);

  // Whether a `ComponentEventListener` has been bound to this realm `ComponentEventProvider`.
  bool HasComponentEventListenerBound();

  // Given a component url |fp|, initializes and returns the component's absolute storage
  // directory for the given |storage_path|. Returns an error if the directory could not be made.
  //
  // A component instance's storage directory is in one of two places:
  //  (a) A directory keyed using component instance ID, if it has one.
  //  (b) A directory computed using fn(realm_path, component URL)
  //
  // If a component is assigned an instance ID while it already has a storage
  // directory under (b), its storage directory is moved to (a).
  //
  // Note: This method is public only for the purposes for testing storage paths and migration
  // logic.
  fit::result<std::string, zx_status_t> InitIsolatedPathForComponentInstance(
      const FuchsiaPkgUrl& fp, internal::StorageType storage_type);

  // Shutdown realm's namespace processing all pending messages.
  void ShutdownNamespace(ShutdownNamespaceCallback callback = nullptr);

  static Moniker ComputeMoniker(Realm* realm, const FuchsiaPkgUrl& fp);

 private:
  static uint32_t next_numbered_label_;

  // Returns |runner| if it exists in |runners_|, otherwise creates a runner in
  // |runners_|. If |use_parent_runners_| is true, creates |runner| in
  // |parent_->runners_|.
  RunnerHolder* GetOrCreateRunner(const std::string& runner);
  Realm* GetRunnerRealm();

  void CreateComponentWithRunnerForScheme(std::string runner_url,
                                          fuchsia::sys::LaunchInfo launch_info,
                                          ComponentRequestWrapper component_request,
                                          ComponentObjectCreatedCallback callback);

  void CreateComponentFromPackage(fuchsia::sys::PackagePtr package,
                                  fuchsia::sys::LaunchInfo launch_info,
                                  ComponentRequestWrapper component_request,
                                  ComponentObjectCreatedCallback callback);

  void CreateElfBinaryComponentFromPackage(
      fuchsia::sys::LaunchInfo launch_info, zx::vmo executable, const std::string& app_argv0,
      const std::vector<std::string>& env_vars, zx::channel loader_service,
      fdio_flat_namespace_t* flat, ComponentRequestWrapper component_request,
      fxl::RefPtr<Namespace> ns, const std::vector<zx_policy_basic_v2_t>& policies,
      ComponentObjectCreatedCallback callback, zx::channel package_handle);

  void CreateRunnerComponentFromPackage(
      fuchsia::sys::PackagePtr package, fuchsia::sys::LaunchInfo launch_info,
      RuntimeMetadata& runtime, fuchsia::sys::FlatNamespace flat,
      ComponentRequestWrapper component_request, fxl::RefPtr<Namespace> ns,
      fidl::VectorPtr<fuchsia::sys::ProgramMetadata> program_metadata, zx::channel package_handle);

  // When a component event will be triggered, this finds what provider to notify and with what
  // identity data. The provider will be either the one attached to this component or some
  // provider in an ancestor realm.
  internal::EventNotificationInfo GetEventNotificationInfo(const std::string& component_url,
                                                           const std::string& component_name,
                                                           const std::string& instance_id);

  zx::channel OpenInfoDir();

  // Called by `FindComponent`. This function returns realm path in reverse order.
  zx::status<fuchsia::sys::internal::SourceIdentity> FindComponentInternal(zx_koid_t process_koid);

  // Registers a job for crash introspection.
  // This internally adds realm label to passed |component_info| and calls either it's own parent
  // or directly calls introspector if this is a root realm.
  void RegisterJobForCrashIntrospection(const zx::job& job,
                                        fuchsia::sys::internal::SourceIdentity component_info);

  fxl::WeakPtr<Realm> const parent_;
  fuchsia::sys::LoaderPtr loader_;
  std::string label_;
  std::string data_path_;
  std::string cache_path_;
  std::string temp_path_;
  std::string koid_;
  std::vector<std::string> realm_path_;
  const bool run_virtual_console_;
  std::unique_ptr<component::PackageLoader> package_loader_;
  std::unique_ptr<component::CacheControl> cache_control_;
  fbl::RefPtr<LogConnectorImpl> log_connector_;

  zx::job job_;

  fxl::RefPtr<Namespace> default_namespace_;

  std::unique_ptr<component::ComponentEventProviderImpl> component_event_provider_;

  RealmHub hub_;
  fs::SynchronousVfs info_vfs_;

  std::unordered_map<Realm*, std::unique_ptr<EnvironmentControllerImpl>> children_;

  std::unordered_map<ComponentControllerImpl*, std::shared_ptr<ComponentControllerImpl>>
      applications_;

  std::unordered_map<std::string, std::unique_ptr<RunnerHolder>> runners_;

  // This channel pair is only created for the root realm, and is used to
  // implement BindFirstNestedRealmSvc. The server end is used to serve the
  // services directory (/svc) for the first nested realm created.
  zx::channel first_nested_realm_svc_client_;
  zx::channel first_nested_realm_svc_server_;

  SchemeMap scheme_map_;

  const std::shared_ptr<sys::ServiceDirectory> environment_services_;

  fxl::UniqueFD appmgr_config_dir_;

  bool use_parent_runners_ = false;
  bool delete_storage_on_death_ = false;

  // Pointer to a cpu watcher to register and unregister components for sampling.
  // Not owned.
  CpuWatcher* const cpu_watcher_;

  fbl::RefPtr<ComponentIdIndex> component_id_index_;

  fxl::WeakPtrFactory<Realm> weak_ptr_factory_;

  // Implement crash introspect service. Only initialized in root realm.
  std::unique_ptr<CrashIntrospector> crash_introspector_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Realm);
};

}  // namespace component

#endif  // SRC_SYS_APPMGR_REALM_H_
