// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/sessionmgr/puppet_master/puppet_master_impl.h"

#include <lib/fxl/logging.h>

#include "peridot/bin/sessionmgr/puppet_master/story_puppet_master_impl.h"
#include "peridot/bin/sessionmgr/storage/session_storage.h"

namespace modular {

PuppetMasterImpl::PuppetMasterImpl(SessionStorage* const session_storage,
                                   StoryCommandExecutor* const executor)
    : session_storage_(session_storage), executor_(executor) {
  FXL_DCHECK(session_storage_ != nullptr);
  FXL_DCHECK(executor_ != nullptr);
}

PuppetMasterImpl::~PuppetMasterImpl() = default;

void PuppetMasterImpl::Connect(
    fidl::InterfaceRequest<fuchsia::modular::PuppetMaster> request) {
  bindings_.AddBinding(this, std::move(request));
}

void PuppetMasterImpl::ControlStory(
    fidl::StringPtr story_name,
    fidl::InterfaceRequest<fuchsia::modular::StoryPuppetMaster> request) {
  auto controller = std::make_unique<StoryPuppetMasterImpl>(
      story_name, session_storage_, executor_);
  story_puppet_masters_.AddBinding(std::move(controller), std::move(request));
}

void PuppetMasterImpl::WatchSession(
    fidl::InterfaceHandle<fuchsia::modular::SessionWatcher> session_watcher,
    fuchsia::modular::WatchSessionOptionsPtr options,
    WatchSessionCallback done) {
  FXL_NOTIMPLEMENTED();
}

void PuppetMasterImpl::DeleteStory(fidl::StringPtr story_name,
                                   DeleteStoryCallback done) {
  session_storage_->DeleteStory(story_name)->Then([done = std::move(done)] {
    done();
  });
}

}  // namespace modular
