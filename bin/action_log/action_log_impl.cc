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
#include "third_party/rapidjson/rapidjson/stringbuffer.h"
#include "third_party/rapidjson/rapidjson/writer.h"

namespace maxwell {

UserActionLogImpl::UserActionLogImpl(ProposalPublisherPtr proposal_publisher)
    : action_log_([this](const std::string& component_url,
                         const std::string& method, const std::string& params) {
        BroadcastToSubscribers(component_url, method, params);
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

void UserActionLogImpl::ProposeSharingVideo(const std::string& component_url,
                                            const std::string& method,
                                            const std::string& params) {
  if (method.compare("ViewVideo") == 0) {
    // TODO(azani): Put information relevant to the video in the proposal.
    ProposalPtr proposal(Proposal::New());
    proposal->id = "";
    proposal->on_selected = fidl::Array<ActionPtr>::New(0);
    proposal->display = SuggestionDisplay::New();
    proposal->display->headline = "Share Video";
    proposal->display->subheadline = "";
    proposal->display->details = "";
    proposal->display->icon_urls = fidl::Array<fidl::String>::New(0);
    proposal->display->image_url = "";
    proposal_publisher_->Propose(std::move(proposal));
  }
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
