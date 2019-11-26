// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_TEST_RUNNER_CPP_SCOPE_H_
#define LIB_TEST_RUNNER_CPP_SCOPE_H_

#include <fuchsia/sys/cpp/fidl.h>

#include <memory>
#include <string>

#include <fs/pseudo_dir.h>
#include <fs/service.h>
#include <fs/synchronous_vfs.h>

#include "lib/fidl/cpp/binding.h"
#include "lib/fidl/cpp/interface_request.h"

namespace test_runner {

// A container of services to pass to Scope.
class ScopeServices {
 public:
  ScopeServices();
  ScopeServices(const ScopeServices&) = delete;
  ScopeServices& operator=(const ScopeServices&) = delete;
  ScopeServices(ScopeServices&&) = delete;

  template <typename Interface>
  zx_status_t AddService(fidl::InterfaceRequestHandler<Interface> handler,
                         const std::string& service_name = Interface::Name_) {
    svc_names_.push_back(service_name);
    return svc_->AddEntry(
        service_name.c_str(),
        fbl::AdoptRef(new fs::Service([handler = std::move(handler)](zx::channel channel) {
          handler(fidl::InterfaceRequest<Interface>(std::move(channel)));
          return ZX_OK;
        })));
  }

 private:
  friend class Scope;
  zx::channel OpenAsDirectory();

  std::unique_ptr<fs::SynchronousVfs> vfs_;
  fbl::RefPtr<fs::PseudoDir> svc_;
  std::vector<std::string> svc_names_;
};

// Provides fate separation of sets of applications run by one application. The
// environment services are delegated to the parent environment. The storage
// backing this environment is deleted when this instance goes out of scope.
class Scope {
 public:
  Scope(const fuchsia::sys::EnvironmentPtr& parent_env, const std::string& label,
        std::unique_ptr<ScopeServices> services);

  fuchsia::sys::Launcher* GetLauncher();

  fuchsia::sys::EnvironmentPtr& environment() { return env_; }

 private:
  std::unique_ptr<ScopeServices> services_;
  fuchsia::sys::EnvironmentPtr env_;
  fuchsia::sys::LauncherPtr env_launcher_;
  fuchsia::sys::EnvironmentControllerPtr env_controller_;
};

}  // namespace test_runner

#endif  // LIB_TEST_RUNNER_CPP_SCOPE_H_
