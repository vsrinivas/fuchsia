// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_LIB_FIDL_ENVIRONMENT_H_
#define SRC_MODULAR_LIB_FIDL_ENVIRONMENT_H_

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/component_context.h>

#include <memory>
#include <string>
#include <vector>

#include <fbl/ref_ptr.h>

#include "src/lib/storage/vfs/cpp/pseudo_dir.h"
#include "src/lib/storage/vfs/cpp/service.h"
#include "src/lib/storage/vfs/cpp/synchronous_vfs.h"

namespace modular {

// Provides fate separation of sets of applications run by one application. The
// environment services are delegated to the parent environment.
class Environment {
 public:
  Environment(const fuchsia::sys::EnvironmentPtr& parent_env, const std::string& label,
              const std::vector<std::string>& service_names, bool kill_on_oom);

  Environment(const Environment* parent_scope, const std::string& label,
              const std::vector<std::string>& service_names, bool kill_on_oom);

  template <typename Interface>
  void AddService(fidl::InterfaceRequestHandler<Interface> handler,
                  const std::string& service_name = Interface::Name_) {
    auto service =
        fbl::MakeRefCounted<fs::Service>([handler = std::move(handler)](zx::channel channel) {
          handler(fidl::InterfaceRequest<Interface>(std::move(channel)));
          return ZX_OK;
        });
    services_dir_->AddEntry(service_name, service);
  }

  // Overrides return value of GetLauncher() with |launcher|.
  void OverrideLauncher(std::unique_ptr<fuchsia::sys::Launcher> launcher);

  fuchsia::sys::Launcher* GetLauncher();

  const fuchsia::sys::EnvironmentPtr& environment() const { return env_; }

 private:
  zx::channel OpenAsDirectory();

  void InitEnvironment(const fuchsia::sys::EnvironmentPtr& parent_env, const std::string& label,
                       const std::vector<std::string>& service_names, bool kill_on_oom);

  fuchsia::sys::EnvironmentPtr env_;
  fuchsia::sys::LauncherPtr env_launcher_;
  fuchsia::sys::EnvironmentControllerPtr env_controller_;
  fs::SynchronousVfs vfs_;
  fbl::RefPtr<fs::PseudoDir> services_dir_;

  std::unique_ptr<fuchsia::sys::Launcher> override_launcher_;
};

}  // namespace modular

#endif  // SRC_MODULAR_LIB_FIDL_ENVIRONMENT_H_
