// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "src/connectivity/overnet/lib/embedded/basic_overnet_embedded.h"
#include "src/connectivity/overnet/lib/embedded/stream_client.h"
#include "src/connectivity/overnet/lib/protocol/reliable_framer.h"

namespace overnet {

class OvernetEmbedded : public BasicOvernetEmbedded {
 public:
  OvernetEmbedded(
      IpAddr ascendd_addr = *overnet::IpAddr::Unix("/tmp/ascendd.socket"))
      : stream_client_{this, ascendd_addr} {}

 private:
  overnet::StreamClient<overnet::ReliableFramer> stream_client_;
};

}  // namespace overnet
