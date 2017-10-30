// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_ACTION_LOG_ACTION_LOG_DATA_H_
#define PERIDOT_BIN_ACTION_LOG_ACTION_LOG_DATA_H_

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "lib/user_intelligence/fidl/scope.fidl.h"

namespace maxwell {

struct ActionData {
  const std::string story_id;
  const std::string component_url;
  const std::vector<std::string> module_path;
  const std::string method;
  const std::string params;
};

using ActionLogger =
    std::function<void(const std::string& method, const std::string& params)>;

using ActionListener = std::function<void(const ActionData& action_data)>;

class ActionLogData {
 public:
  ActionLogData(ActionListener listener);

  ActionLogger GetActionLogger(ComponentScopePtr scope);
  // TODO(azani): Make the log readable somehow.

  void Append(const ActionData& action_data);

 private:
  std::vector<ActionData> log_;
  ActionListener listener_;
};

}  // namespace maxwell

#endif  // PERIDOT_BIN_ACTION_LOG_ACTION_LOG_DATA_H_
