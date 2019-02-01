// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_NETWORK_TIME_TIMEZONE_H_
#define GARNET_BIN_NETWORK_TIME_TIMEZONE_H_

#include <lib/zx/time.h>
#include <sys/time.h>

#include <string>
#include <utility>

namespace time_server {

// TODO(CP-131): Rename to something like SystemTimeUpdater.
class Timezone {
 public:
  bool Run();
  bool UpdateSystemTime(int tries);
  static bool SetSystemTime(zx::time_utc time);
  Timezone(std::string server_config_file)
      : server_config_file_(std::move(server_config_file)) {}
  ~Timezone() = default;

 private:
  std::string server_config_file_;
};

}  // namespace time_server

#endif  // GARNET_BIN_NETWORK_TIME_TIMEZONE_H_
