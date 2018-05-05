// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/guest/mgr/guest_manager_impl.h"

#include "lib/fxl/logging.h"

namespace guestmgr {

static uint32_t g_next_env_id = 0;

GuestManagerImpl::GuestManagerImpl()
    : context_(component::ApplicationContext::CreateFromStartupInfo()) {
  context_->outgoing().AddPublicService<guest::GuestManager>(
      [this](fidl::InterfaceRequest<guest::GuestManager> request) {
        bindings_.AddBinding(this, std::move(request));
      });
}

GuestManagerImpl::~GuestManagerImpl() = default;

void GuestManagerImpl::CreateEnvironment(
    fidl::StringPtr label,
    fidl::InterfaceRequest<guest::GuestEnvironment> request) {
  uint32_t env_id = g_next_env_id++;
  auto env = std::make_unique<GuestEnvironmentImpl>(
      env_id, label, context_.get(), std::move(request));
  environments_.insert({env_id, std::move(env)});
}

void GuestManagerImpl::ListEnvironments(ListEnvironmentsCallback callback) {
  fidl::VectorPtr<guest::GuestEnvironmentInfo> env_infos =
      fidl::VectorPtr<guest::GuestEnvironmentInfo>::New(0);
  for (const auto& env : environments_) {
    fidl::VectorPtr<guest::GuestInfo> guest_infos =
        fidl::VectorPtr<guest::GuestInfo>::New(0);
    guest::GuestEnvironmentInfo env_info;
    env_info.id = env.first;
    env_info.label = env.second->label();
    env_info.guests = env.second->ListGuests();
    env_infos.push_back(std::move(env_info));
  }
  callback(std::move(env_infos));
}

void GuestManagerImpl::ConnectToEnvironment(
    uint32_t id,
    fidl::InterfaceRequest<guest::GuestEnvironment> request) {
  const auto& it = environments_.find(id);
  if (it == environments_.end()) {
    return;
  }
  it->second->AddBinding(std::move(request));
}

}  // namespace guestmgr
