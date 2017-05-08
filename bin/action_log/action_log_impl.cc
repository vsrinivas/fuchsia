// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/src/action_log/action_log_impl.h"

#include "lib/ftl/logging.h"

#include "apps/maxwell/src/action_log/action_log_data.h"

#include "third_party/rapidjson/rapidjson/document.h"
#include "third_party/rapidjson/rapidjson/stringbuffer.h"
#include "third_party/rapidjson/rapidjson/writer.h"

namespace maxwell {

UserActionLogImpl::UserActionLogImpl()
    : action_log_([this](const std::string& component_url,
                         const std::string& method, const std::string& params) {
        BroadcastToSubscribers(component_url, method, params);
      }) {}

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
