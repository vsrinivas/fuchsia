// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/acquirers/story_info/story_info.h"

#include <sstream>

#include "peridot/bin/acquirers/story_info/story_watcher_impl.h"
#include "peridot/lib/fidl/json_xdr.h"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace maxwell {

using ::fuchsia::maxwell::internal::StoryInfoInitializer;

StoryInfoAcquirer::StoryInfoAcquirer(modular::AgentHost* const agent_host)
    : initializer_binding_(this),
      visible_stories_watcher_binding_(this),
      story_provider_watcher_binding_(this),
      focus_watcher_binding_(this) {
  // Initialize fuchsia::modular::IntelligenceServices.
  fuchsia::modular::IntelligenceServicesPtr intelligence_services;
  agent_host->agent_context()->GetIntelligenceServices(
      intelligence_services.NewRequest());
  intelligence_services->GetContextWriter(context_writer_.NewRequest());
  intelligence_services->GetContextReader(context_reader_.NewRequest());

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

void StoryInfoAcquirer::Connect(
    fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> services) {
  agent_services_.AddBinding(std::move(services));
}

void StoryInfoAcquirer::RunTask(
    const fidl::StringPtr& task_id,
    const fuchsia::modular::Agent::RunTaskCallback& callback) {
  FXL_LOG(FATAL) << "Not implemented.";
}

void StoryInfoAcquirer::Terminate(const std::function<void()>& done) { done(); }

void StoryInfoAcquirer::Initialize(
    fidl::InterfaceHandle<fuchsia::modular::StoryProvider> story_provider,
    fidl::InterfaceHandle<fuchsia::modular::FocusProvider> focus_provider,
    fidl::InterfaceHandle<fuchsia::modular::VisibleStoriesProvider>
        visible_stories_provider) {
  story_provider_.Bind(std::move(story_provider));
  focus_provider_.Bind(std::move(focus_provider));

  // Watch for changes to what Stories are visible.
  auto visible_stories_provider_ptr = visible_stories_provider.Bind();
  visible_stories_provider_ptr->Watch(
      visible_stories_watcher_binding_.NewBinding());

  // Watch for changes in Story state.
  story_provider_->Watch(story_provider_watcher_binding_.NewBinding());

  // Watch for changes in the focused Story.
  focus_provider_->Watch(focus_watcher_binding_.NewBinding());

  // Write initial values for visible stories.
  OnVisibleStoriesChange({});
}

void StoryInfoAcquirer::OnFocusChange(fuchsia::modular::FocusInfoPtr info) {
  // Set all stories to *not* focused, then set the one that's focused to
  // "focused".
  for (const auto& e : stories_) {
    if (!info->focused_story_id || e.first != info->focused_story_id) {
      e.second->OnFocusChange(false);
    }
  }
  if (info->focused_story_id) {
    auto it = stories_.find(info->focused_story_id);
    if (it == stories_.end()) {
      FXL_LOG(ERROR)
          << "RACE CONDITION: I was notified that story "
          << info->focused_story_id
          << " was focused before being notified it exists in the first place.";
      return;
    }
    it->second->OnFocusChange(true);
  }
}

void StoryInfoAcquirer::OnVisibleStoriesChange(
    fidl::VectorPtr<fidl::StringPtr> ids) {
  // TODO(thatguy)
}

void StoryInfoAcquirer::OnChange(
    fuchsia::modular::StoryInfo info, fuchsia::modular::StoryState state,
    fuchsia::modular::StoryVisibilityState visibility_state) {
  // Here we only check if a story is new, and if so create a StoryWatcherImpl.
  // We proxy all future change events to it.
  auto it = stories_.find(info.id);
  if (it == stories_.end()) {
    auto ret = stories_.emplace(std::make_pair(
        info.id,
        std::make_unique<StoryWatcherImpl>(this, context_writer_.get(),
                                           story_provider_.get(), info.id)));
    it = ret.first;
  }
  it->second->OnStoryStateChange(std::move(info), state);
}

void StoryInfoAcquirer::OnDelete(fidl::StringPtr story_id) {
  const std::string id = story_id.get();
  // TODO(thatguy)
}

}  // namespace maxwell
