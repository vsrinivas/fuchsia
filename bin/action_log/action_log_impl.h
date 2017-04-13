// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "apps/maxwell/services/action_log/action_log.fidl.h"
#include "apps/maxwell/src/action_log/action_log_data.h"

#include "lib/fidl/cpp/bindings/binding_set.h"

namespace maxwell {

class ActionLogFactoryImpl : public ActionLogFactory {
 public:
  ActionLogFactoryImpl();

 private:
  // |ActionLogFactory|
  void GetActionLog(
      ComponentScopePtr scope,
      fidl::InterfaceRequest<ActionLog> action_log_request) override;

  std::shared_ptr<ActionLogData> action_log_;
  fidl::BindingSet<ActionLog, std::unique_ptr<ActionLog>>
      module_action_log_bindings_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ActionLogFactoryImpl);
};

class ActionLogImpl : public ActionLog {
 public:
  ActionLogImpl(ActionLogger log_action) : log_action_(log_action) {}

  void LogAction(const fidl::String& method,
                 const fidl::String& params) override;

 private:
  const ActionLogger log_action_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ActionLogImpl);
};
}  // namespace maxwell
