// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_TIME_LIB_NETWORK_TIME_TIME_SERVER_CONFIG_H_
#define SRC_SYS_TIME_LIB_NETWORK_TIME_TIME_SERVER_CONFIG_H_

#include <string>
#include <vector>

#include "src/sys/time/lib/network_time/roughtime_server.h"

namespace time_server {

class TimeServerConfig {
 public:
  bool Parse(std::string server_config_file);
  std::vector<RoughTimeServer> ServerList();

 private:
  std::vector<RoughTimeServer> server_list_;
};

}  // namespace time_server

#endif  // SRC_SYS_TIME_LIB_NETWORK_TIME_TIME_SERVER_CONFIG_H_
