// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sstream>

#include "apps/maxwell/services/user/intelligence_services.fidl.h"
#include "apps/maxwell/src/acquirers/story_info/story_info.h"

namespace maxwell {

namespace {

std::string CreateKey(const std::string& story_id, const std::string suffix) {
  std::stringstream s;
  s << "/story/id/" << story_id << "/" << suffix;
  return s.str();
}

}  // namespace

StoryInfoAcquirer::StoryInfoAcquirer()
    : initializer_binding_(this),
      visible_stories_watcher_binding_(this),
      story_provider_watcher_binding_(this) {
  // This ServiceProvider is handed out in Connect().
  agent_services_.AddService<StoryInfoInitializer>(
      [this](fidl::InterfaceRequest<StoryInfoInitializer> request) {
        FTL_DCHECK(!initializer_binding_.is_bound());
        initializer_binding_.Bind(std::move(request));
      });
}

StoryInfoAcquirer::~StoryInfoAcquirer() = default;

void StoryInfoAcquirer::Initialize(
    fidl::InterfaceHandle<modular::AgentContext> agent_context_handle,
    const InitializeCallback& callback) {
  // Initialize |context_publiser_| using IntelligenceServices.
  auto agent_context =
      modular::AgentContextPtr::Create(std::move(agent_context_handle));
  IntelligenceServicesPtr intelligence_services;
  agent_context->GetIntelligenceServices(intelligence_services.NewRequest());
  intelligence_services->GetContextPublisher(context_publisher_.NewRequest());
}

void StoryInfoAcquirer::Connect(
    const fidl::String& requestor_url,
    fidl::InterfaceRequest<app::ServiceProvider> services) {
  agent_services_.AddBinding(std::move(services));
}

void StoryInfoAcquirer::RunTask(const fidl::String& task_id,
                                const RunTaskCallback& callback) {
  FTL_LOG(FATAL) << "Not implemented.";
}

void StoryInfoAcquirer::Stop(const StopCallback& callback) {
  // TODO(thatguy): Stop all watchers, reset all InterfacePtrs (close channels).
  callback();
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
}

void StoryInfoAcquirer::OnVisibleStoriesChange(fidl::Array<fidl::String> ids) {
  for (std::string id : ids) {
    auto key = CreateKey(id, "visible");  // remove
    FTL_LOG(INFO) << key << " = 1";       // remove
    context_publisher_->Publish(CreateKey(id, "visible"), "1");
  }
}

void StoryInfoAcquirer::OnChange(modular::StoryInfoPtr info) {
  FTL_LOG(INFO) << "Got new info for " << info->id;
}

void StoryInfoAcquirer::OnDelete(const fidl::String& story_id) {
  FTL_LOG(INFO) << "Story deleted: " << story_id;
}

}  // namespace maxwell
