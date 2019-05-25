// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/ref_ptr.h>

#include "src/connectivity/overnet/lib/embedded/udp_nub.h"
#include "src/connectivity/overnet/lib/omdp/omdp.h"

namespace overnet {

class OmdpNub final : public BasicOvernetEmbedded::Actor, private Omdp {
 public:
  OmdpNub(BasicOvernetEmbedded* root, UdpNub* udp_nub);
  ~OmdpNub();
  Status Start() override;
  const char* Name() const override { return "OmdpNub"; }

 private:
  void OnNewNode(uint64_t node_id, IpAddr addr) override;
  void Broadcast(Slice slice) override;
  void WaitForInbound();
  void InboundReady(const Status& status);

  HostReactor* const reactor_;
  UdpNub* const udp_nub_;
  Timer* const timer_;
  Socket incoming_;
};

}  // namespace overnet
