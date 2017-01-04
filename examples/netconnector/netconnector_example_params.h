// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>

#include "lib/ftl/command_line.h"
#include "lib/ftl/macros.h"

namespace examples {

class NetConnectorExampleParams {
 public:
  NetConnectorExampleParams(const ftl::CommandLine& command_line);

  bool is_valid() const { return is_valid_; }

  bool register_provider() const { return register_provider_; }

  const std::string& request_device_name() const {
    return request_device_name_;
  }

 private:
  void Usage();

  bool is_valid_;
  bool register_provider_;
  std::string request_device_name_;

  FTL_DISALLOW_COPY_AND_ASSIGN(NetConnectorExampleParams);
};

}  // namespace examples
