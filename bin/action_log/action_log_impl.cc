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
    : action_log_([this](const ActionData& action_data) {
        BroadcastToSubscribers(action_data);
        MaybeProposeSharingVideo(action_data);
      }),
      proposal_publisher_(std::move(proposal_publisher)) {
}

void UserActionLogImpl::BroadcastToSubscribers(const ActionData& action_data) {
  UserActionPtr action(UserAction::New());
  action->component_url = action_data.component_url;
  action->method = action_data.method;
  action->parameters = action_data.params;
  subscribers_.ForAllPtrs([action = std::move(action)](
      ActionLogListener * listener) { listener->OnAction(action.Clone()); });
}

void UserActionLogImpl::MaybeProposeSharingVideo(
    const ActionData& action_data) {
  if (action_data.method.compare("ViewVideo") != 0) {
    return;
  }
  // TODO(azani): Put information relevant to the video in the proposal.
  rapidjson::Document doc_params;
  if (doc_params.Parse(action_data.params).HasParseError()) {
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

  auto add_module = AddModuleToStory::New();
  add_module->story_id = action_data.story_id;
  add_module->module_id = "file:///system/apps/email/composer";
  add_module->link_name = "email-composer-link";
  // TODO(azani): Do something sane.
  std::string initial_data = "{\"email-composer\": {\"message\": {";
  initial_data += "\"subject\": \"Really cool video!!!!1one\",";
  initial_data += "\"text\": \"http://www.youtube.com/watch?v=";
  initial_data += video_id + "\"}}}";
  add_module->initial_data = initial_data;

  auto action = Action::New();
  action->set_add_module_to_story(std::move(add_module));
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
  std::unique_ptr<ComponentActionLogImpl> module_action_log_impl(
      new ComponentActionLogImpl(
          action_log_.GetActionLogger(std::move(scope))));

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
