// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/netconnector/examples/netconnector_example/netconnector_example_params.h"

#include "lib/ftl/logging.h"

namespace examples {

NetConnectorExampleParams::NetConnectorExampleParams(
    const ftl::CommandLine& command_line) {
  is_valid_ = false;

  if (!command_line.GetOptionValue("request-device", &request_device_name_)) {
    request_device_name_.clear();
  }

  is_valid_ = true;
}

void NetConnectorExampleParams::Usage() {
  FTL_LOG(INFO) << "netconnector_example usage:";
  FTL_LOG(INFO) << "    @boot netconnector_example [ --request-device=<name> ]";
}

}  // namespace examples
