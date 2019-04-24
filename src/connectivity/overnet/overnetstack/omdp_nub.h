// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/ref_ptr.h>
#include <fuchsia/net/cpp/fidl.h>

#include "src/connectivity/overnet/lib/omdp/omdp.h"
#include "src/connectivity/overnet/overnetstack/overnet_app.h"
#include "src/connectivity/overnet/overnetstack/udp_nub.h"

namespace overnetstack {

class OmdpNub final : public OvernetApp::Actor, public overnet::Omdp {
 public:
  OmdpNub(OvernetApp* app, UdpNub* udp_nub);
  ~OmdpNub();
  overnet::Status Start() override;

 private:
  void OnNewNode(uint64_t node_id, overnet::IpAddr addr) override;
  void Broadcast(overnet::Slice slice) override;
  void WaitForInbound();
  void InboundReady(zx_status_t status, uint32_t events);
  void EnsureIncoming();

  UdpNub* const udp_nub_;
  overnet::Socket incoming_;
  fsl::FDWaiter fd_waiter_;
};

}  // namespace overnetstack
