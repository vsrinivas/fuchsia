// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/lib/fidl/environment.h"

#include <lib/async/default.h>

#include "src/lib/syslog/cpp/logger.h"

namespace modular {

Environment::Environment(const fuchsia::sys::EnvironmentPtr& parent_env, const std::string& label,
                         const std::vector<std::string>& service_names, bool kill_on_oom)
    : vfs_(async_get_default_dispatcher()) {
  InitEnvironment(parent_env, label, service_names, kill_on_oom);
}

Environment::Environment(const Environment* const parent_env, const std::string& label,
                         const std::vector<std::string>& service_names, bool kill_on_oom)
    : vfs_(async_get_default_dispatcher()) {
  FX_DCHECK(parent_env != nullptr);
  InitEnvironment(parent_env->environment(), label, service_names, kill_on_oom);
}

void Environment::OverrideLauncher(std::unique_ptr<fuchsia::sys::Launcher> launcher) {
  override_launcher_ = std::move(launcher);
}

fuchsia::sys::Launcher* Environment::GetLauncher() {
  if (override_launcher_) {
    return override_launcher_.get();
  }
  if (!env_launcher_) {
    env_->GetLauncher(env_launcher_.NewRequest());
  }
  return env_launcher_.get();
}

zx::channel Environment::OpenAsDirectory() {
  zx::channel h1, h2;
  if (zx::channel::create(0, &h1, &h2) != ZX_OK)
    return zx::channel();
  if (vfs_.ServeDirectory(services_dir_, std::move(h1)) != ZX_OK)
    return zx::channel();
  return h2;
}

void Environment::InitEnvironment(const fuchsia::sys::EnvironmentPtr& parent_env,
                                  const std::string& label,
                                  const std::vector<std::string>& service_names, bool kill_on_oom) {
  services_dir_ = fbl::AdoptRef(new fs::PseudoDir);
  fuchsia::sys::ServiceListPtr service_list(new fuchsia::sys::ServiceList);
  for (const auto& name : service_names) {
    service_list->names.push_back(name);
  }
  service_list->host_directory = OpenAsDirectory();
  parent_env->CreateNestedEnvironment(
      env_.NewRequest(), env_controller_.NewRequest(), label, std::move(service_list),
      {.inherit_parent_services = true, .kill_on_oom = kill_on_oom});
}

}  // namespace modular
