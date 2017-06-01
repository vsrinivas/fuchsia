// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/src/action_log/action_log_impl.h"

#include "lib/ftl/logging.h"
#include "lib/ftl/time/time_delta.h"
#include "lib/mtl/tasks/message_loop.h"

#include "apps/maxwell/services/suggestion/proposal.fidl.h"
#include "apps/maxwell/services/suggestion/suggestion_display.fidl.h"
#include "apps/maxwell/src/action_log/action_log_data.h"

#include "third_party/rapidjson/rapidjson/document.h"
#include "third_party/rapidjson/rapidjson/pointer.h"
#include "third_party/rapidjson/rapidjson/stringbuffer.h"
#include "third_party/rapidjson/rapidjson/writer.h"

namespace maxwell {

UserActionLogImpl::UserActionLogImpl(ProposalPublisherPtr proposal_publisher)
    : action_log_([this](const std::string& component_url,
                         const std::string& method, const std::string& params) {
        BroadcastToSubscribers(component_url, method, params);
        MaybeProposeSharingVideo(component_url, method, params);
      }),
      proposal_publisher_(std::move(proposal_publisher)) {
  // TODO(azani): Remove before production!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
  LogDummyActionDelayed();
}

void UserActionLogImpl::BroadcastToSubscribers(const std::string& component_url,
                                               const std::string& method,
                                               const std::string& params) {
  UserActionPtr action(UserAction::New());
  action->component_url = component_url;
  action->method = method;
  action->parameters = params;
  subscribers_.ForAllPtrs([action = std::move(action)](
      ActionLogListener * listener) { listener->OnAction(action.Clone()); });
}

void UserActionLogImpl::MaybeProposeSharingVideo(
    const std::string& component_url, const std::string& method,
    const std::string& params) {
  if (method.compare("ViewVideo") != 0) {
    return;
  }
  // TODO(azani): Put information relevant to the video in the proposal.
  rapidjson::Document doc_params;
  if (doc_params.Parse(params).HasParseError()) {
    return;
  }
  rapidjson::Value* vid_value =
      rapidjson::Pointer("/youtube-doc/youtube-video-id").Get(doc_params);

  if (vid_value == nullptr || !vid_value->IsString()) {
    return;
  }
  std::string video_id = vid_value->GetString();
  std::string proposal_id = "Share Video " + video_id;

  ProposalPtr proposal(Proposal::New());
  proposal->id = proposal_id;

  auto create_story = CreateStory::New();
  create_story->module_id = "file:///system/apps/email/composer";
  // TODO(azani): Do something sane.
  std::string initial_data = "{\"email-composer\": {\"message\": {";
  initial_data += "\"subject\": \"Really cool video!!!!1one\",";
  initial_data += "\"text\": \"http://www.youtube.com/watch?v=";
  initial_data += video_id + "\"}}}";
  create_story->initial_data = initial_data;

  auto action = Action::New();
  action->set_create_story(std::move(create_story));
  proposal->on_selected.push_back(std::move(action));

  auto display = SuggestionDisplay::New();
  display->headline = proposal_id;
  display->subheadline = "";
  display->details = "";
  display->color = 0xff42ebf4;
  display->icon_urls.push_back("");
  display->image_url = "";
  display->image_type = SuggestionImageType::OTHER;
  proposal->display = std::move(display);

  proposal_publisher_->Propose(std::move(proposal));
}

void UserActionLogImpl::GetComponentActionLog(
    ComponentScopePtr scope,
    fidl::InterfaceRequest<ComponentActionLog> action_log_request) {
  std::string component_url;
  if (scope->is_agent_scope()) {
    component_url = scope->get_agent_scope()->url;
  } else if (scope->is_module_scope()) {
    component_url = scope->get_module_scope()->url;
  }
  std::unique_ptr<ComponentActionLogImpl> module_action_log_impl(
      new ComponentActionLogImpl(action_log_.GetActionLogger(component_url)));

  action_log_bindings_.AddBinding(std::move(module_action_log_impl),
                                  std::move(action_log_request));
}

void UserActionLogImpl::Duplicate(
    fidl::InterfaceRequest<UserActionLog> request) {
  bindings_.AddBinding(this, std::move(request));
}

void UserActionLogImpl::Subscribe(
    fidl::InterfaceHandle<ActionLogListener> listener_handle) {
  ActionLogListenerPtr listener =
      ActionLogListenerPtr::Create(std::move(listener_handle));
  subscribers_.AddInterfacePtr(std::move(listener));
  // TODO(azani): Remove when dummy data is no longer needed.
  BroadcastToSubscribers("http://example.org", "SpuriousMethod",
                         "{\"cake_truth\": false}");
}

void UserActionLogImpl::LogDummyActionDelayed() {
  mtl::MessageLoop::GetCurrent()->task_runner()->PostDelayedTask(
      [this] {
        action_log_.Append("http://example.org", "SpuriousMethod",
                           "{\"cake_truth\": false}");
        LogDummyActionDelayed();
      },
      ftl::TimeDelta::FromSeconds(5));
}

void ComponentActionLogImpl::LogAction(const fidl::String& method,
                                       const fidl::String& json_params) {
  rapidjson::Document params;
  if (params.Parse(json_params.get().c_str()).HasParseError()) {
    FTL_LOG(WARNING) << "Parse error.";
    return;
  }

  log_action_(method, json_params);
}

}  // namespace maxwell
