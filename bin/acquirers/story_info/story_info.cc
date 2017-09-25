// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/acquirers/story_info/story_info.h"

#include <sstream>

#include "lib/context/fidl/value.fidl.h"
#include "lib/user_intelligence/fidl/intelligence_services.fidl.h"
#include "peridot/bin/acquirers/story_info/modular.h"
#include "peridot/bin/acquirers/story_info/story_watcher_impl.h"
#include "peridot/lib/fidl/json_xdr.h"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace maxwell {

StoryInfoAcquirer::StoryInfoAcquirer()
    : initializer_binding_(this),
      visible_stories_watcher_binding_(this),
      story_provider_watcher_binding_(this),
      focus_watcher_binding_(this) {
  // This ServiceProvider is handed out in Connect().
  agent_services_.AddService<StoryInfoInitializer>(
      [this](fidl::InterfaceRequest<StoryInfoInitializer> request) {
        FXL_DCHECK(!initializer_binding_.is_bound());
        initializer_binding_.Bind(std::move(request));
      });
}

StoryInfoAcquirer::~StoryInfoAcquirer() = default;

void StoryInfoAcquirer::DropStoryWatcher(const std::string& story_id) {
  stories_.erase(story_id);
}

void StoryInfoAcquirer::Initialize(
    fidl::InterfaceHandle<modular::AgentContext> agent_context_handle,
    const InitializeCallback& callback) {
  // Initialize |context_publiser_| using IntelligenceServices.
  auto agent_context =
      modular::AgentContextPtr::Create(std::move(agent_context_handle));
  IntelligenceServicesPtr intelligence_services;
  agent_context->GetIntelligenceServices(intelligence_services.NewRequest());
  intelligence_services->GetContextWriter(context_writer_.NewRequest());
  intelligence_services->GetContextReader(context_reader_.NewRequest());
  callback();
}

void StoryInfoAcquirer::Connect(
    const fidl::String& requestor_url,
    fidl::InterfaceRequest<app::ServiceProvider> services) {
  agent_services_.AddBinding(std::move(services));
}

void StoryInfoAcquirer::RunTask(const fidl::String& task_id,
                                const RunTaskCallback& callback) {
  FXL_LOG(FATAL) << "Not implemented.";
}

void StoryInfoAcquirer::Initialize(
    fidl::InterfaceHandle<modular::StoryProvider> story_provider,
    fidl::InterfaceHandle<modular::FocusProvider> focus_provider,
    fidl::InterfaceHandle<modular::VisibleStoriesProvider>
        visible_stories_provider) {
  story_provider_.Bind(std::move(story_provider));
  focus_provider_.Bind(std::move(focus_provider));

  // Watch for changes to what Stories are visible.
  auto visible_stories_provider_ptr =
      modular::VisibleStoriesProviderPtr::Create(
          std::move(visible_stories_provider));
  visible_stories_provider_ptr->Watch(
      visible_stories_watcher_binding_.NewBinding());

  // Watch for changes in Story state.
  story_provider_->Watch(story_provider_watcher_binding_.NewBinding());

  // Watch for changes in the focused Story.
  focus_provider_->Watch(focus_watcher_binding_.NewBinding());

  // Write initial values for visible stories.
  OnVisibleStoriesChange({});
}

void StoryInfoAcquirer::OnFocusChange(modular::FocusInfoPtr info) {
  // Set all stories to *not* focused, then set the one that's focused to
  // "focused".
  for (const auto& e : stories_) {
    if (!info->focused_story_id || e.first != info->focused_story_id) {
      e.second->OnFocusChange(false);
    }
  }
  if (info->focused_story_id) {
    stories_[info->focused_story_id]->OnFocusChange(true);
  }
}

void StoryInfoAcquirer::OnVisibleStoriesChange(fidl::Array<fidl::String> ids) {
  // TODO(thatguy)
}

void StoryInfoAcquirer::OnChange(modular::StoryInfoPtr info,
                                 modular::StoryState state) {
  // Here we only check if a story is new, and if so create a StoryWatcherImpl.
  // We proxy all future change events to it.
  auto it = stories_.find(info->id);
  if (it == stories_.end()) {
    auto ret = stories_.emplace(std::make_pair(
        info->id,
        std::make_unique<StoryWatcherImpl>(this, context_writer_.get(),
                                           story_provider_.get(), info->id)));
    it = ret.first;
  }
  it->second->OnStoryStateChange(std::move(info), state);
}

void StoryInfoAcquirer::OnDelete(const fidl::String& story_id) {
  const std::string id = story_id.get();
  // TODO(thatguy)
}

}  // namespace maxwell
