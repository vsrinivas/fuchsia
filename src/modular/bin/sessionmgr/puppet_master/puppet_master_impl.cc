// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/sessionmgr/puppet_master/puppet_master_impl.h"

#include "src/lib/fxl/logging.h"
#include "src/modular/bin/sessionmgr/puppet_master/story_puppet_master_impl.h"
#include "src/modular/bin/sessionmgr/storage/session_storage.h"

namespace modular {

PuppetMasterImpl::PuppetMasterImpl(SessionStorage* const session_storage,
                                   StoryCommandExecutor* const executor)
    : session_storage_(session_storage), executor_(executor) {
  FXL_DCHECK(session_storage_ != nullptr);
  FXL_DCHECK(executor_ != nullptr);
}

PuppetMasterImpl::~PuppetMasterImpl() = default;

void PuppetMasterImpl::Connect(fidl::InterfaceRequest<fuchsia::modular::PuppetMaster> request) {
  bindings_.AddBinding(this, std::move(request));
}

void PuppetMasterImpl::ControlStory(
    std::string story_name, fidl::InterfaceRequest<fuchsia::modular::StoryPuppetMaster> request) {
  auto controller = std::make_unique<StoryPuppetMasterImpl>(story_name, &operations_,
                                                            session_storage_, executor_);
  story_puppet_masters_.AddBinding(std::move(controller), std::move(request));
}

void PuppetMasterImpl::DeleteStory(std::string story_name, DeleteStoryCallback done) {
  // Remove StoryPuppetMasters to stop pending commands executing after delete.
  std::vector<StoryPuppetMasterImpl*> to_remove;
  for (auto& binding : story_puppet_masters_.bindings()) {
    if (binding->impl()->story_name() == story_name)
      to_remove.emplace_back(binding->impl().get());
  }
  for (auto* impl : to_remove) {
    story_puppet_masters_.RemoveBinding(impl);
  }

  // Delete the Story storage.
  session_storage_->DeleteStory(story_name)->Then(std::move(done));
}

void PuppetMasterImpl::GetStories(GetStoriesCallback done) {
  session_storage_->GetAllStoryData()->Then(
      [done = std::move(done)](
          const std::vector<fuchsia::modular::internal::StoryData>& all_story_data) {
        std::vector<std::string> result;
        result.reserve(all_story_data.size());

        for (auto& story : all_story_data) {
          result.push_back(story.story_info().id());
        }

        done(std::move(result));
      });
}

}  // namespace modular
