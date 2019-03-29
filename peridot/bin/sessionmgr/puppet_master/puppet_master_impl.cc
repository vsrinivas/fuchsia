// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/sessionmgr/puppet_master/puppet_master_impl.h"

#include <src/lib/fxl/logging.h>

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
    std::string story_name,
    fidl::InterfaceRequest<fuchsia::modular::StoryPuppetMaster> request) {
  auto controller = std::make_unique<StoryPuppetMasterImpl>(
      story_name, &operations_, session_storage_, executor_);
  story_puppet_masters_.AddBinding(std::move(controller), std::move(request));
}

void PuppetMasterImpl::DeleteStory(std::string story_name,
                                   DeleteStoryCallback done) {
  session_storage_->DeleteStory(story_name)->Then(std::move(done));
}

void PuppetMasterImpl::GetStories(GetStoriesCallback done) {
  session_storage_->GetAllStoryData()->Then(
      [done = std::move(done)](
          std::vector<fuchsia::modular::internal::StoryData> all_story_data) {
        std::vector<std::string> result;
        for (auto& story : all_story_data) {
          result.push_back(std::move(story.story_info().id));
        }

        done(std::move(result));
      });
}

}  // namespace modular
