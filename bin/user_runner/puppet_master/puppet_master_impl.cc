// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/user_runner/puppet_master/puppet_master_impl.h"

#include "lib/fxl/logging.h"

namespace modular {

PuppetMasterImpl::PuppetMasterImpl() = default;
PuppetMasterImpl::~PuppetMasterImpl() = default;

void PuppetMasterImpl::Connect(fidl::InterfaceRequest<PuppetMaster> request) {
  bindings_.AddBinding(this, std::move(request));
}

void PuppetMasterImpl::ControlStory(
    fidl::StringPtr story_id,
    fidl::InterfaceRequest<StoryPuppetMaster> request) {
  FXL_NOTIMPLEMENTED();
}

void PuppetMasterImpl::WatchSession(
    WatchSessionParams params,
    fidl::InterfaceHandle<SessionWatcher> session_watcher,
    WatchSessionCallback done) {
  FXL_NOTIMPLEMENTED();
}

}  // namespace modular
