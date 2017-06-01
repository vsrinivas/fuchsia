// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/src/action_log/action_log_data.h"

#include "lib/ftl/logging.h"

#include "third_party/rapidjson/rapidjson/document.h"
#include "third_party/rapidjson/rapidjson/stringbuffer.h"
#include "third_party/rapidjson/rapidjson/writer.h"

namespace maxwell {

ActionLogData::ActionLogData(ActionListener listener) : listener_(listener) {}

ActionLogger ActionLogData::GetActionLogger(ComponentScopePtr scope) {
  std::string component_url;
  std::string story_id = "";
  if (scope->is_agent_scope()) {
    component_url = scope->get_agent_scope()->url;
  } else if (scope->is_module_scope()) {
    component_url = scope->get_module_scope()->url;
    story_id = scope->get_module_scope()->story_id;
  }

  return [this, story_id, component_url](const std::string& method,
                                         const std::string& params) {
    ActionData action{story_id, component_url, method, params};
    Append(action);
  };
}

void ActionLogData::Append(const ActionData& action_data) {
  rapidjson::Document params;
  FTL_CHECK(!params.Parse(action_data.params).HasParseError());
  listener_(action_data);
  log_.push_back(action_data);
}

}  // namespace maxwell
