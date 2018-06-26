// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/action_log/action_log_data.h"

#include "lib/fxl/logging.h"
#include "third_party/rapidjson/rapidjson/document.h"
#include "third_party/rapidjson/rapidjson/stringbuffer.h"
#include "third_party/rapidjson/rapidjson/writer.h"

namespace modular {

ActionLogData::ActionLogData(ActionListener listener) : listener_(listener) {}

ActionLogData::~ActionLogData() = default;

ActionLogger ActionLogData::GetActionLogger(
    fuchsia::modular::ComponentScope scope) {
  std::string component_url;
  std::string story_id = "";
  std::vector<std::string> module_path;
  if (scope.is_agent_scope()) {
    component_url = scope.agent_scope().url;
  } else if (scope.is_module_scope()) {
    component_url = scope.module_scope().url;
    story_id = scope.module_scope().story_id;
    module_path.insert(module_path.begin(),
                       scope.module_scope().module_path->begin(),
                       scope.module_scope().module_path->end());
  }

  return [this, story_id, component_url, module_path](
             const std::string& method, const std::string& params) {
    ActionData action{story_id, component_url, module_path, method, params};
    Append(action);
  };
}

void ActionLogData::Append(const ActionData& action_data) {
  rapidjson::Document params;
  FXL_CHECK(!params.Parse(action_data.params).HasParseError());
  listener_(action_data);
  log_.push_back(action_data);
}

}  // namespace modular
