// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_APPMGR_REALM_H_
#define SRC_SYS_APPMGR_REALM_H_

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/zx/channel.h>
#include <zircon/syscalls/policy.h>

#include <iosfwd>
#include <memory>
#include <string>
#include <unordered_map>

#include <fs/synchronous_vfs.h>

#include "garnet/lib/cmx/runtime.h"
#include "garnet/lib/loader/package_loader.h"
#include "src/lib/files/unique_fd.h"
#include "src/lib/fsl/vmo/file.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/memory/ref_ptr.h"
#include "src/lib/fxl/strings/string_view.h"
#include "src/sys/appmgr/cache_control.h"
#include "src/sys/appmgr/component_container.h"
#include "src/sys/appmgr/component_controller_impl.h"
#include "src/sys/appmgr/environment_controller_impl.h"
#include "src/sys/appmgr/hub/hub_info.h"
#include "src/sys/appmgr/hub/realm_hub.h"
#include "src/sys/appmgr/namespace.h"
#include "src/sys/appmgr/namespace_builder.h"
#include "src/sys/appmgr/runner_holder.h"
#include "src/sys/appmgr/scheme_map.h"

namespace component {

struct RealmArgs {
  static RealmArgs Make(Realm* parent, std::string label, std::string data_path,
                        std::string cache_path, std::string temp_path,
                        const std::shared_ptr<sys::ServiceDirectory>& env_services,
                        bool run_virtual_console, fuchsia::sys::EnvironmentOptions options,
                        fxl::UniqueFD appmgr_config_dir);

  static RealmArgs MakeWithAdditionalServices(
      Realm* parent, std::string label, std::string data_path, std::string cache_path,
      std::string temp_path, const std::shared_ptr<sys::ServiceDirectory>& env_services,
      bool run_virtual_console, fuchsia::sys::ServiceListPtr additional_services,
      fuchsia::sys::EnvironmentOptions options, fxl::UniqueFD appmgr_config_dir);

  Realm* parent;
  std::string label;
  std::string data_path;
  std::string cache_path;
  std::string temp_path;
  std::shared_ptr<sys::ServiceDirectory> environment_services;
  bool run_virtual_console;
  fuchsia::sys::ServiceListPtr additional_services;
  fuchsia::sys::EnvironmentOptions options;
  fxl::UniqueFD appmgr_config_dir;
};

class Realm : public ComponentContainer<ComponentControllerImpl> {
 public:
  static std::unique_ptr<Realm> Create(RealmArgs args);

  // Constructor to create a Realm object. Clients should call |Create|.
  Realm(RealmArgs args, zx::job job);

  ~Realm();

  Realm* parent() const { return parent_; }
  const std::string& label() const { return label_; }
  const std::string& data_path() const { return data_path_; }
  const std::string& cache_path() const { return cache_path_; }
  const std::string& temp_path() const { return temp_path_; }
  const std::string& koid() const { return koid_; }

  const fbl::RefPtr<fs::PseudoDir>& hub_dir() const { return hub_.dir(); }

  std::shared_ptr<sys::ServiceDirectory> environment_services() const {
    return environment_services_;
  }

  HubInfo HubInfo();

  zx::job DuplicateJobForHub() const;

  const zx::job& job() const { return job_; }

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

  bool IsAllowedToUseDeprecatedShell(std::string ns_id);
  bool IsAllowedToUseDeprecatedAmbientReplaceAsExecutable(std::string ns_id);

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

  void CreateElfBinaryComponentFromPackage(fuchsia::sys::LaunchInfo launch_info,
                                           zx::vmo executable, const std::string& app_argv0,
                                           const std::vector<std::string>& env_vars,
                                           zx::channel loader_service, fdio_flat_namespace_t* flat,
                                           ComponentRequestWrapper component_request,
                                           fxl::RefPtr<Namespace> ns,
                                           const std::vector<zx_policy_basic_t>& policies,
                                           ComponentObjectCreatedCallback callback);

  void CreateRunnerComponentFromPackage(
      fuchsia::sys::PackagePtr package, fuchsia::sys::LaunchInfo launch_info,
      RuntimeMetadata& runtime, fuchsia::sys::FlatNamespace flat,
      ComponentRequestWrapper component_request, fxl::RefPtr<Namespace> ns,
      fidl::VectorPtr<fuchsia::sys::ProgramMetadata> program_metadata);

  zx::channel OpenInfoDir();

  std::string IsolatedPathForPackage(std::string path_prefix, const FuchsiaPkgUrl& fp);

  Realm* const parent_;
  fuchsia::sys::LoaderPtr loader_;
  std::string label_;
  std::string data_path_;
  std::string cache_path_;
  std::string temp_path_;
  std::string koid_;
  const bool run_virtual_console_;
  std::unique_ptr<component::PackageLoader> package_loader_;
  std::unique_ptr<component::CacheControl> cache_control_;

  zx::job job_;

  fxl::RefPtr<Namespace> default_namespace_;

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

  FXL_DISALLOW_COPY_AND_ASSIGN(Realm);
};

}  // namespace component

#endif  // SRC_SYS_APPMGR_REALM_H_
