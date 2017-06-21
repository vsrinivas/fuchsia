// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sstream>

#include "apps/maxwell/services/user/intelligence_services.fidl.h"
#include "apps/maxwell/src/acquirers/story_info/story_info.h"
#include "apps/maxwell/src/context_engine/scope_utils.h"
#include "apps/modular/lib/fidl/json_xdr.h"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace maxwell {

namespace {

const std::string kStoryPrefix = "/story";

std::string CreateKey(const std::string suffix) {
  std::stringstream s;
  s << kStoryPrefix << "/" << suffix;
  return s.str();
}

std::string CreateKey(const std::string& story_id, const std::string suffix) {
  std::stringstream s;
  s << kStoryPrefix << "/id/" << story_id << "/" << suffix;
  return s.str();
}

std::string StoryStateToString(modular::StoryState state) {
  // INITIAL
  // STARTING
  // RUNNING
  // DONE
  // STOPPED
  // ERROR
  switch (state) {
    case modular::StoryState::INITIAL:
      return "INITIAL";
    case modular::StoryState::STARTING:
      return "STARTING";
    case modular::StoryState::RUNNING:
      return "RUNNING";
    case modular::StoryState::DONE:
      return "DONE";
    case modular::StoryState::STOPPED:
      return "STOPPED";
    case modular::StoryState::ERROR:
      return "ERROR";
    default:
      FTL_LOG(FATAL) << "Unknown modular::StoryState value: " << state;
  }
}

}  // namespace

/// StoryWatcherImpl

// TODO(thatguy): This is currently duplicated from
// apps/modular/src/story_runner/story_storage_impl.cc.
void XdrLinkPath(modular::XdrContext* const xdr,
                 modular::LinkPath* const data) {
  xdr->Field("module_path", &data->module_path);
  xdr->Field("link_name", &data->link_name);
}

void XdrModuleData(modular::XdrContext* const xdr,
                   modular::ModuleData* const data) {
  xdr->Field("url", &data->module_url);
  xdr->Field("module_path", &data->module_path);
  xdr->Field("default_link_path", &data->link_path, XdrLinkPath);
  xdr->Field("module_source", &data->module_source);
}

class StoryWatcherImpl : public modular::StoryWatcher {
 public:
  StoryWatcherImpl(ContextPublisher* publisher, const fidl::String& story_id)
      : story_id_(story_id), publisher_(publisher) {}

 private:
  // |StoryWatcher|
  void OnStateChange(modular::StoryState new_state) override {
    // Handled in StoryInfoAcquirer::OnChange()
  }

  // |StoryWatcher|
  void OnModuleAdded(modular::ModuleDataPtr module_data) override {
    std::string meta;
    modular::XdrWrite(&meta, &module_data, XdrModuleData);
    publisher_->Publish(
        MakeModuleScopeTopic(story_id_, module_data->module_path, "meta"),
        meta);
  }

  const fidl::String story_id_;
  ContextPublisher* publisher_;
};

/// StoryInfoAcquirer

StoryInfoAcquirer::StoryInfoAcquirer()
    : initializer_binding_(this),
      visible_stories_watcher_binding_(this),
      story_provider_watcher_binding_(this),
      focus_watcher_binding_(this) {
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
  callback();
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

  // Watch for changes in the focused Story.
  focus_provider_->Watch(focus_watcher_binding_.NewBinding());

  // Write initial values for visible stories.
  OnVisibleStoriesChange({});
}

void StoryInfoAcquirer::OnFocusChange(modular::FocusInfoPtr info) {
  std::string value;
  modular::XdrWrite(&value, &info->focused_story_id,
                    modular::XdrFilter<fidl::String>);
  context_publisher_->Publish(CreateKey("focused_id"), value);
}

void StoryInfoAcquirer::OnVisibleStoriesChange(fidl::Array<fidl::String> ids) {
  std::string array_value;
  modular::XdrWrite(&array_value, &ids, modular::XdrFilter<fidl::String>);
  context_publisher_->Publish(CreateKey("visible_ids"), array_value);
  context_publisher_->Publish(CreateKey("visible_count"),
                              std::to_string(ids.size()));
}

void StoryInfoAcquirer::OnChange(modular::StoryInfoPtr info,
                                 modular::StoryState state) {
  const std::string id = info->id.get();

  // If we aren't already watching this story, get a controller and create
  // a watcher.
  if (known_story_ids_.count(id) == 0) {
    modular::StoryControllerPtr story_controller;
    story_provider_->GetController(info->id, story_controller.NewRequest());
    story_controller->Watch(story_watcher_bindings_.AddBinding(
        std::make_unique<StoryWatcherImpl>(context_publisher_.get(), id)));
    known_story_ids_.insert(id);
  }

  std::string url_json;
  modular::XdrWrite(&url_json, &info->url, modular::XdrFilter<fidl::String>);
  context_publisher_->Publish(CreateKey(id, "url"), url_json);
  std::string state_text = StoryStateToString(state);
  std::string state_json;
  modular::XdrWrite(&state_json, &state_text, modular::XdrFilter<std::string>);
  context_publisher_->Publish(CreateKey(id, "state"), state_json);
  context_publisher_->Publish(CreateKey(id, "deleted"), "false");
}

void StoryInfoAcquirer::OnDelete(const fidl::String& story_id) {
  const std::string id = story_id.get();
  context_publisher_->Publish(CreateKey(id, "deleted"), "true");
}

}  // namespace maxwell
