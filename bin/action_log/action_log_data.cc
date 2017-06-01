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

ActionLogger ActionLogData::GetActionLogger(const std::string& module_url) {
  return [this, module_url](const std::string& method, const std::string& params) {
    Append(module_url, method, params);
  };
}

void ActionLogData::Append(
    const std::string& module_url,
    const std::string& method,
    const std::string& json_params) {
  listener_(module_url, method, json_params);
  rapidjson::Document params;
  FTL_CHECK(!params.Parse(json_params).HasParseError());

  rapidjson::Document action;
  action.SetObject();
  action.AddMember("module",
                   rapidjson::Value(module_url, action.GetAllocator()),
                   action.GetAllocator());
  action.AddMember("method",
                   rapidjson::Value(method, action.GetAllocator()),
                   action.GetAllocator());
  action.AddMember("params", params.GetObject(), action.GetAllocator());

  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  action.Accept(writer);
}

}  // namespace maxwell
