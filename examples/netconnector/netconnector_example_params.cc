// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/netconnector/examples/netconnector_example/netconnector_example_params.h"

#include "lib/ftl/logging.h"

namespace examples {

NetConnectorExampleParams::NetConnectorExampleParams(
    const ftl::CommandLine& command_line) {
  is_valid_ = false;

  register_provider_ = command_line.HasOption("register-provider");

  if (!command_line.GetOptionValue("request-device", &request_device_name_)) {
    request_device_name_.clear();
  } else if (register_provider_) {
    // Can't request-device if we register-provider.
    Usage();
    return;
  }

  is_valid_ = true;
}

void NetConnectorExampleParams::Usage() {
  FTL_LOG(INFO) << "netconnector_example usage:";
  FTL_LOG(INFO) << "    @boot netconnector_example [ options ]";
  FTL_LOG(INFO) << "options:";
  FTL_LOG(INFO)
      << "    --request-device=<name>   request example service from device";
  FTL_LOG(INFO)
      << "    --register-provider       register example service provider";
  FTL_LOG(INFO) << "options are mutually exclusive";
}

}  // namespace examples
