// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/test_runner/cpp/scope.h"
#include <lib/async/default.h>

namespace test_runner {

ScopeServices::ScopeServices()
    : vfs_(
          std::make_unique<fs::SynchronousVfs>(async_get_default_dispatcher())),
      svc_(fbl::AdoptRef(new fs::PseudoDir)) {}

zx::channel ScopeServices::OpenAsDirectory() {
  zx::channel h1, h2;
  if (zx::channel::create(0, &h1, &h2) != ZX_OK)
    return zx::channel();
  if (vfs_->ServeDirectory(svc_, std::move(h1)) != ZX_OK)
    return zx::channel();
  return h2;
}

Scope::Scope(const fuchsia::sys::EnvironmentPtr& parent_env,
             const std::string& label, std::unique_ptr<ScopeServices> services)
    : services_(std::move(services)) {
  fuchsia::sys::ServiceListPtr service_list(new fuchsia::sys::ServiceList);
  service_list->names = std::move(services_->svc_names_);
  service_list->host_directory = services_->OpenAsDirectory();
  parent_env->CreateNestedEnvironment(
      env_.NewRequest(), env_controller_.NewRequest(), label,
      std::move(service_list),
      {.inherit_parent_services = true, .delete_storage_on_death = true});
}

fuchsia::sys::Launcher* Scope::GetLauncher() {
  if (!env_launcher_) {
    env_->GetLauncher(env_launcher_.NewRequest());
  }
  return env_launcher_.get();
}

}  // namespace test_runner
