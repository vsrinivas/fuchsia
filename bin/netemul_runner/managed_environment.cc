// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "managed_environment.h"

namespace netemul {

using component::testing::EnclosingEnvironment;
using component::testing::EnvironmentServices;

ManagedEnvironment::Ptr ManagedEnvironment::CreateRoot(
    const fuchsia::sys::EnvironmentPtr& parent,
    const SandboxEnv::Ptr& sandbox_env) {
  auto ret = ManagedEnvironment::Ptr(new ManagedEnvironment(sandbox_env));
  Options options;
  options.name = "root";
  ret->Create(parent, std::move(options));
  return ret;
}

ManagedEnvironment::ManagedEnvironment(const SandboxEnv::Ptr& sandbox_env)
    : sandbox_env_(sandbox_env) {}
component::testing::EnclosingEnvironment& ManagedEnvironment::environment() {
  return *env_;
}

void ManagedEnvironment::GetLauncher(
    ::fidl::InterfaceRequest<::fuchsia::sys::Launcher> launcher) {
  launcher_->Bind(std::move(launcher));
}

void ManagedEnvironment::CreateChildEnvironment(
    fidl::InterfaceRequest<FManagedEnvironment> me, Options options) {
  ManagedEnvironment::Ptr np(new ManagedEnvironment(sandbox_env_));
  fuchsia::sys::EnvironmentPtr env;
  env_->ConnectToService(env.NewRequest());
  np->Create(env, std::move(options));
  np->bindings_.AddBinding(np.get(), std::move(me));

  children_.emplace_back(std::move(np));
}

void ManagedEnvironment::Create(const fuchsia::sys::EnvironmentPtr& parent,
                                ManagedEnvironment::Options options) {
  auto services = EnvironmentServices::Create(parent);

  // add network context service:
  services->AddService(sandbox_env_->network_context().GetHandler());

  // add managed environment itself as a handler
  services->AddService(bindings_.GetHandler(this));

  // push all the allowable launch services:
  for (const auto& svc : options.services) {
    fuchsia::sys::LaunchInfo linfo;
    linfo.url = svc.url;
    services->AddServiceWithLaunchInfo(std::move(linfo), svc.name);
  }

  // save all handles for virtual devices
  for (auto& dev : options.devices) {
    virtual_devices_.AddEntry(dev.path, dev.device.Bind());
  }

  fuchsia::sys::EnvironmentOptions sub_options = {
      .kill_on_oom = true,
      .allow_parent_runners = false,
      .inherit_parent_services = true};

  env_ = EnclosingEnvironment::Create(options.name, parent, std::move(services),
                                      sub_options);

  env_->SetRunningChangedCallback([this](bool running) {
    if (running && running_callback_) {
      running_callback_();
    }
  });

  launcher_ = std::make_unique<ManagedLauncher>(this);
}

zx::channel ManagedEnvironment::OpenVdevDirectory() {
  return virtual_devices_.OpenAsDirectory();
}

zx::channel ManagedEnvironment::OpenVdataDirectory() {
  if (!virtual_data_) {
    virtual_data_ = std::make_unique<VirtualData>();
  }
  return virtual_data_->GetDirectory();
}

}  // namespace netemul
