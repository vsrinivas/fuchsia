// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "src/connectivity/overnet/lib/embedded/basic_overnet_embedded.h"
#include "src/connectivity/overnet/lib/links/packet_nub.h"
#include "src/connectivity/overnet/lib/vocabulary/ip_addr.h"
#include "src/connectivity/overnet/lib/vocabulary/socket.h"

namespace overnet {

using UdpNubBase = PacketNub<IpAddr, 1500, HashIpAddr, EqIpAddr>;

class UdpNub final : public BasicOvernetEmbedded::Actor, public UdpNubBase {
 public:
  explicit UdpNub(BasicOvernetEmbedded* app);
  ~UdpNub();

  const char* Name() const override { return "UdpNub"; }
  Status Start() override;
  uint16_t port() { return port_; }
  NodeId node_id() { return endpoint_->node_id(); }
  void SendTo(IpAddr addr, Slice slice) override;

  void Publish(overnet::LinkPtr<> link) override {
    NodeId node = NodeId(link->GetLinkStatus().to);
    OVERNET_TRACE(DEBUG) << "NewLink: " << node << "\n";
    endpoint_->RegisterPeer(node);
    endpoint_->RegisterLink(std::move(link));
  }

  Socket* socket() { return &socket_; }

 private:
  RouterEndpoint* const endpoint_;
  Timer* const timer_;
  Socket socket_;
  uint16_t port_ = -1;
  HostReactor* const reactor_;

  void WaitForInbound();
  void InboundReady(const Status& status);
  Status CreateFD();
  Status SetOptionSharePort();
  Status SetOptionReceiveAnything();
  Status Bind();
  Status StatusFromErrno(const std::string& why);
};

}  // namespace overnet
