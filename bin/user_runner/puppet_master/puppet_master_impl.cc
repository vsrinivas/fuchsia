// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/user_runner/puppet_master/puppet_master_impl.h"

#include <lib/fxl/logging.h>

#include "peridot/bin/user_runner/puppet_master/story_puppet_master_impl.h"

namespace modular {

PuppetMasterImpl::PuppetMasterImpl(StoryCommandExecutor* const executor)
    : executor_(executor) {
  FXL_DCHECK(executor_ != nullptr);
}

PuppetMasterImpl::~PuppetMasterImpl() = default;

void PuppetMasterImpl::Connect(
    fidl::InterfaceRequest<fuchsia::modular::PuppetMaster> request) {
  bindings_.AddBinding(this, std::move(request));
}

void PuppetMasterImpl::ControlStory(
    fidl::StringPtr story_id,
    fidl::InterfaceRequest<fuchsia::modular::StoryPuppetMaster> request) {
  auto controller =
      std::make_unique<StoryPuppetMasterImpl>(story_id, executor_);
  story_puppet_masters_.AddBinding(std::move(controller), std::move(request));
}

void PuppetMasterImpl::WatchSession(
    fidl::InterfaceHandle<fuchsia::modular::SessionWatcher> session_watcher,
    fuchsia::modular::WatchSessionOptionsPtr options,
    WatchSessionCallback done) {
  FXL_NOTIMPLEMENTED();
}

}  // namespace modular
