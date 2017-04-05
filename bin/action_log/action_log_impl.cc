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

ActionLogFactoryImpl::ActionLogFactoryImpl() {
  action_log_ = std::make_shared<ActionLogData>();
}

void ActionLogFactoryImpl::GetActionLog(
    const fidl::String& module_url,
    fidl::InterfaceRequest<ActionLog> action_log_request) {
  ActionLogImpl *module_action_log_impl = new ActionLogImpl(
      action_log_->GetActionLogger(module_url));

  module_action_log_bindings_.AddBinding(
      module_action_log_impl, std::move(action_log_request));
}

void ActionLogImpl::LogAction(const fidl::String& method,
                              const fidl::String& json_params) {
  rapidjson::Document params;
  if (params.Parse(json_params.get().c_str()).HasParseError()) {
    FTL_LOG(WARNING) << "Parse error.";
    return;
  }

  log_action_(method, json_params);
}

}
