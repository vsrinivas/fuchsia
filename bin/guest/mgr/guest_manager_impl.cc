// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/guest/mgr/guest_manager_impl.h"

#include "lib/fxl/logging.h"

namespace guestmgr {

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
  auto env = std::make_unique<GuestEnvironmentImpl>(context_.get(), label,
                                                    std::move(request));
  environments_.insert({env.get(), std::move(env)});
}

void GuestManagerImpl::ListGuests(ListGuestsCallback callback) {
  fidl::VectorPtr<guest::GuestInfo> infos =
      fidl::VectorPtr<guest::GuestInfo>::New(0);
  for (const auto& env : environments_) {
    for (const auto guest : env.first->guests()) {
      guest::GuestInfo info;
      info.id = guest->id();
      info.label = guest->label();
      infos.push_back(info);
    }
  }
  callback(std::move(infos));
}

void GuestManagerImpl::Connect(
    uint32_t guest_id,
    fidl::InterfaceRequest<guest::GuestController> controller) {
  for (const auto& env : environments_) {
    for (auto guest : env.first->guests()) {
      if (guest->id() == guest_id) {
        guest->Bind(std::move(controller));
        return;
      }
    }
  }
}

}  // namespace guestmgr
