// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/guest_manager/manager_impl.h"

#include "src/lib/fxl/logging.h"

static uint32_t g_next_env_id = 0;

ManagerImpl::ManagerImpl() : context_(sys::ComponentContext::Create()) {
  context_->outgoing()->AddPublicService(bindings_.GetHandler(this));
}

void ManagerImpl::Create(fidl::StringPtr label,
                         fidl::InterfaceRequest<fuchsia::virtualization::Realm> request) {
  uint32_t env_id = g_next_env_id++;
  auto env =
      std::make_unique<RealmImpl>(env_id, label.value_or(""), context_.get(), std::move(request));
  env->set_unbound_handler([this, env_id]() { environments_.erase(env_id); });
  environments_.insert({env_id, std::move(env)});
}

void ManagerImpl::List(ListCallback callback) {
  std::vector<fuchsia::virtualization::EnvironmentInfo> env_infos;
  for (const auto& env : environments_) {
    std::vector<fuchsia::virtualization::InstanceInfo> instance_infos;
    fuchsia::virtualization::EnvironmentInfo env_info;
    env_info.id = env.first;
    env_info.label = env.second->label();
    env_info.instances = env.second->ListGuests();
    env_infos.push_back(std::move(env_info));
  }
  callback(std::move(env_infos));
}

void ManagerImpl::Connect(uint32_t id,
                          fidl::InterfaceRequest<fuchsia::virtualization::Realm> request) {
  const auto& it = environments_.find(id);
  if (it != environments_.end()) {
    it->second->AddBinding(std::move(request));
  }
}
