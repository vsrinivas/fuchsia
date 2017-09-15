// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once
#include <stdint.h>

#include <string>
#include <utility>

namespace timeservice {

class TimeService {
 public:
  bool Run();
  bool UpdateSystemTime(uint8_t tries);
  TimeService(std::string server_config_file)
      : server_config_file_(std::move(server_config_file)) {}
  ~TimeService() = default;

 private:
  std::string server_config_file_;
};

}  // namespace timeservice
