// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/guest_manager/environment_manager_impl.h"

#include <src/lib/fxl/logging.h>

static uint32_t g_next_env_id = 0;

EnvironmentManagerImpl::EnvironmentManagerImpl()
    : context_(component::StartupContext::CreateFromStartupInfo()) {
  context_->outgoing().AddPublicService(bindings_.GetHandler(this));
}

void EnvironmentManagerImpl::Create(
    fidl::StringPtr label,
    fidl::InterfaceRequest<fuchsia::guest::EnvironmentController> request) {
  uint32_t env_id = g_next_env_id++;
  auto env = std::make_unique<EnvironmentControllerImpl>(
      env_id, label, context_.get(), std::move(request));
  env->set_unbound_handler([this, env_id]() { environments_.erase(env_id); });
  environments_.insert({env_id, std::move(env)});
}

void EnvironmentManagerImpl::List(ListCallback callback) {
  fidl::VectorPtr<fuchsia::guest::EnvironmentInfo> env_infos =
      fidl::VectorPtr<fuchsia::guest::EnvironmentInfo>::New(0);
  for (const auto& env : environments_) {
    fidl::VectorPtr<fuchsia::guest::InstanceInfo> instance_infos =
        fidl::VectorPtr<fuchsia::guest::InstanceInfo>::New(0);
    fuchsia::guest::EnvironmentInfo env_info;
    env_info.id = env.first;
    env_info.label = env.second->label();
    env_info.instances = env.second->ListGuests();
    env_infos.push_back(std::move(env_info));
  }
  callback(std::move(env_infos));
}

void EnvironmentManagerImpl::Connect(
    uint32_t id,
    fidl::InterfaceRequest<fuchsia::guest::EnvironmentController> request) {
  const auto& it = environments_.find(id);
  if (it != environments_.end()) {
    it->second->AddBinding(std::move(request));
  }
}
