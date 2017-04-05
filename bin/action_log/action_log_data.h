// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace maxwell {

typedef std::function<void(const std::string& method, const std::string& params)> ActionLogger;

class ActionLogData {
 public:
  ActionLogger GetActionLogger(const std::string& module_url);
  // TODO(azani): Make the log readable somehow.

 private:
  void Append(
      const std::string& module_url,
      const std::string& method,
      const std::string& json_params);

  std::vector<std::string> log_;
};

}
