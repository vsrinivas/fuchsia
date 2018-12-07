// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_COMPONENT_CPP_TESTING_ENCLOSING_ENVIRONMENT_H_
#define LIB_COMPONENT_CPP_TESTING_ENCLOSING_ENVIRONMENT_H_

#include <memory>
#include <string>
#include <unordered_map>

#include <fs/pseudo-dir.h>
#include <fs/service.h>
#include <fs/synchronous-vfs.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async/default.h>
#include "lib/component/cpp/testing/launcher_impl.h"
#include "lib/fxl/logging.h"
#include "lib/svc/cpp/services.h"

namespace component {
namespace testing {

class EnclosingEnvironment;

// EnvironmentServices acts as a container of services to EnclosingEnvironment.
//
// By default, EnvironmentServices supplies only the parent environment's loader
// service. Additional services can be provided through |AddService| and
// friends. Typically, this is used to inject fake services for tests, or to
// pass through services from the parent environment.
//
// Every EnclosingEnvironment takes EnvironmentServices as an argument to
// instantiation. Services should not be added after the EnclosingEnvironment is
// created.
class EnvironmentServices {
 public:
  EnvironmentServices(const EnvironmentServices&) = delete;
  EnvironmentServices& operator=(const EnvironmentServices&) = delete;
  EnvironmentServices(EnvironmentServices&&) = delete;

  // Creates services with parent's loader service.
  static std::unique_ptr<EnvironmentServices> Create(
      const fuchsia::sys::EnvironmentPtr& parent_env,
      async_dispatcher_t* dispatcher = nullptr);
  // Creates services with custom loader service.
  static std::unique_ptr<EnvironmentServices> CreateWithCustomLoader(
      const fuchsia::sys::EnvironmentPtr& parent_env,
      const fbl::RefPtr<fs::Service>& loader_service,
      async_dispatcher_t* dispatcher = nullptr);

  // Adds the specified interface to the set of services.
  //
  // Adds a supported service with the given |service_name|, using the given
  // |interface_request_handler|, which should remain valid for the lifetime
  // of this object.
  //
  // A typical usage may be:
  //
  //   AddService(foobar_bindings_.GetHandler(this));
  //
  template <typename Interface>
  zx_status_t AddService(fidl::InterfaceRequestHandler<Interface> handler,
                         const std::string& service_name = Interface::Name_) {
    svc_names_.push_back(service_name);
    return svc_->AddEntry(
        service_name.c_str(),
        fbl::AdoptRef(new fs::Service(
            [handler = std::move(handler)](zx::channel channel) {
              handler(fidl::InterfaceRequest<Interface>(std::move(channel)));
              return ZX_OK;
            })));
  }

  // Adds the specified service to the set of services.
  zx_status_t AddService(const fbl::RefPtr<fs::Service> service,
                         const std::string& service_name);

  // Adds the specified service to the set of services.
  //
  // Adds a supported service with the given |service_name|, using the given
  // |launch_info|, it only starts the component when the service is
  // requested.
  zx_status_t AddServiceWithLaunchInfo(fuchsia::sys::LaunchInfo launch_info,
                                       const std::string& service_name);

  // Allows child components to access parent service with name
  // |service_name|.
  //
  // This will only work if parent environment actually provides said service
  // and the service is in the test component's service whitelist.
  zx_status_t AllowParentService(const std::string& service_name);

 private:
  friend class EnclosingEnvironment;
  EnvironmentServices(const fuchsia::sys::EnvironmentPtr& parent_env,
                      const fbl::RefPtr<fs::Service>& loader_service,
                      async_dispatcher_t* dispatcher = nullptr);

  void set_enclosing_env(EnclosingEnvironment* e) { enclosing_env_ = e; }

  std::unique_ptr<fs::SynchronousVfs> vfs_;
  fbl::RefPtr<fs::PseudoDir> svc_;
  fidl::VectorPtr<fidl::StringPtr> svc_names_;
  fuchsia::sys::ServiceProviderPtr parent_svc_;
  // Pointer to containing environment. Not owned.
  EnclosingEnvironment* enclosing_env_ = nullptr;

  // Keep track of all singleton services, indexed by url.
  std::unordered_map<std::string, component::Services> singleton_services_;
};

// EnclosingEnvironment wraps a new isolated environment for test |parent_env|
// and provides a way to use that environment for integration testing.
//
// It provides a way to add custom fake services using handlers and singleton
// components. By default components under this environment have no access to
// any of system services. You need to add your own services by using
// |AddService| or |AddServiceWithLaunchInfo| methods.
//
// It also provides a way to access parent services if needed.
class EnclosingEnvironment {
 public:
  // Creates environment with the given services.
  //
  // |label| is human readable environment name, it can be seen in /hub, for eg
  // /hub/r/sys/<koid>/r/<label>/<koid>
  //
  // |services| are the services the environment will provide. See
  // |EnvironmentServices| for details.
  static std::unique_ptr<EnclosingEnvironment> Create(
      const std::string& label, const fuchsia::sys::EnvironmentPtr& parent_env,
      std::unique_ptr<EnvironmentServices> services,
      const fuchsia::sys::EnvironmentOptions& options = {});

  ~EnclosingEnvironment();

  fuchsia::sys::LauncherPtr launcher_ptr() {
    fuchsia::sys::LauncherPtr launcher;
    launcher_.AddBinding(launcher.NewRequest());
    return launcher;
  }

  // Returns true if underlying environment is running.
  bool is_running() const { return running_; }

  // Kills the underlying environment.
  void Kill(std::function<void()> callback = nullptr);

  // Creates a real component from |launch_info| in underlying environment.
  //
  // That component will only have access to the services added and
  // any allowed parent service.
  void CreateComponent(
      fuchsia::sys::LaunchInfo launch_info,
      fidl::InterfaceRequest<fuchsia::sys::ComponentController> request);

  // Creates a real component from |launch_info| in underlying environment and
  // returns controller ptr.
  //
  // That component will only have access to the services added and
  // any allowed parent service.
  fuchsia::sys::ComponentControllerPtr CreateComponent(
      fuchsia::sys::LaunchInfo launch_info);

  // Creates a real component in underlying environment for a url and returns
  // controller ptr.
  //
  // That component will only have access to the services added and
  // any allowed parent service.
  fuchsia::sys::ComponentControllerPtr CreateComponentFromUrl(
      std::string component_url);

  // Creates a nested enclosing environment on top of underlying environment.
  std::unique_ptr<EnclosingEnvironment> CreateNestedEnclosingEnvironment(
      const std::string& label);

  // Creates a nested enclosing environment on top of underlying environment
  // with custom loader service.
  std::unique_ptr<EnclosingEnvironment>
  CreateNestedEnclosingEnvironmentWithLoader(
      const std::string& label, fbl::RefPtr<fs::Service> loader_service);

  // Connects to service provided by this environment.
  void ConnectToService(fidl::StringPtr service_name, zx::channel channel) {
    service_provider_->ConnectToService(service_name, std::move(channel));
  }

  // Connects to service provided by this environment.
  template <typename Interface>
  void ConnectToService(fidl::InterfaceRequest<Interface> request,
                        const std::string& service_name = Interface::Name_) {
    ConnectToService(service_name, request.TakeChannel());
  }

  // Sets a listener for changes in the running status
  void SetRunningChangedCallback(fit::function<void(bool)> cb) {
    running_changed_callback_ = std::move(cb);
  }

 private:
  EnclosingEnvironment(const std::string& label,
                       const fuchsia::sys::EnvironmentPtr& parent_env,
                       std::unique_ptr<EnvironmentServices> services,
                       const fuchsia::sys::EnvironmentOptions& options);

  void SetRunning(bool running);

  bool running_ = false;
  const std::string label_;
  fuchsia::sys::EnvironmentControllerPtr env_controller_;
  fuchsia::sys::ServiceProviderPtr service_provider_;
  LauncherImpl launcher_;
  std::unique_ptr<EnvironmentServices> services_;
  fit::function<void(bool)> running_changed_callback_;
};

}  // namespace testing
}  // namespace component

#endif  // LIB_COMPONENT_CPP_TESTING_ENCLOSING_ENVIRONMENT_H_
