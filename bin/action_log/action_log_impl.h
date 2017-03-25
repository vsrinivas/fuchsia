// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "apps/maxwell/services/action_log/action_log.fidl.h"

#include "lib/fidl/cpp/bindings/binding_set.h"

namespace maxwell {

class ActionLogData {
 public:
  void Append(std::string action);
  // TODO(azani): Make the log readable somehow.

 private:
  std::vector<std::string> log_;
};

class ActionLogFactoryImpl : public ActionLogFactory {
 public:
  ActionLogFactoryImpl();

  void GetActionLog(
      const fidl::String& module_url,
      fidl::InterfaceRequest<ActionLog> action_log_request) override;

 private:
  std::shared_ptr<ActionLogData> action_log_;
  fidl::BindingSet<ActionLog> module_action_log_bindings_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ActionLogFactoryImpl);
};

class ActionLogImpl : public ActionLog {
 public:
  ActionLogImpl(const std::string module_url,
      std::shared_ptr<ActionLogData> action_log)
    : module_url_(module_url), action_log_(action_log) {}

  void LogAction(const fidl::String& method,
                 const fidl::String& params) override;

 private:
  std::string module_url_;
  std::shared_ptr<ActionLogData> action_log_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ActionLogImpl);
};
} // namespace maxwell
