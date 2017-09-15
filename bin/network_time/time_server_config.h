// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>
#include <vector>

#include "garnet/bin/network_time/roughtime_server.h"

namespace timeservice {

class TimeServerConfig {
 public:
  bool Parse(std::string server_config_file);
  std::vector<RoughTimeServer> ServerList();

 private:
  std::vector<RoughTimeServer> server_list_;
};

}  // namespace timeservice
