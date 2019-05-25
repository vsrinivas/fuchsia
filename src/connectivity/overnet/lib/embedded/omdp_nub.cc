// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/overnet/lib/embedded/omdp_nub.h"

#include <net/if.h>

#include "garnet/public/lib/fostr/fidl/fuchsia/overnet/omdp/formatting.h"
#include "src/connectivity/overnet/lib/omdp/omdp.h"
#include "src/connectivity/overnet/lib/vocabulary/ip_addr.h"
#include "src/connectivity/overnet/lib/vocabulary/socket.h"

namespace overnet {

std::random_device rng_dev;
static const size_t kMaximumPacketSize = 1400;

OmdpNub::OmdpNub(BasicOvernetEmbedded* root, UdpNub* udp_nub)
    : BasicOvernetEmbedded::Actor(root),
      Omdp(root->node_id().get(), root->timer(), []() { return rng_dev(); }),
      reactor_(root->reactor()),
      udp_nub_(udp_nub),
      timer_(root->timer()) {}

OmdpNub::~OmdpNub() = default;

Status OmdpNub::Start() {
  return incoming_.Create(AF_INET6, SOCK_DGRAM, 0)
      .Then([&] { return incoming_.SetOptReusePort(true); })
      .Then([&] {
        auto addr = *IpAddr::AnyIpv6().WithPort(kMulticastGroupAddr.port());
        OVERNET_TRACE(DEBUG) << "Bind incoming OMDP socket to: " << addr;
        return incoming_.Bind(addr);
      })
      .Then([&] {
        return incoming_.SetOpt(
            IPPROTO_IPV6, IPV6_JOIN_GROUP,
            ipv6_mreq{kMulticastGroupAddr.ipv6.sin6_addr, 0});
      })
      .Then([&] {
        ScheduleBroadcast();
        WaitForInbound();
        return Status::Ok();
      });
}

void OmdpNub::Broadcast(Slice data) {
  auto& socket = *udp_nub_->socket();
  auto* interfaces = if_nameindex();
  if (interfaces == nullptr) {
    OVERNET_TRACE(WARNING) << "Failed to retrieve interface list";
    return;
  }
  OVERNET_TRACE(DEBUG) << "BROADCAST " << data << " to " << kMulticastGroupAddr;
  std::vector<Status> failures;
  bool any_successes = false;
  for (size_t i = 0;
       interfaces[i].if_index != 0 || interfaces[i].if_name != nullptr; i++) {
    if (auto status =
            socket
                .SetOpt(IPPROTO_IPV6, IPV6_MULTICAST_IF, interfaces[i].if_index)
                .Then(
                    [&] { return socket.SendTo(data, 0, kMulticastGroupAddr); })
                .WithLazyContext([&] {
                  std::ostringstream out;
                  out << "On " << interfaces[i].if_name;
                  return out.str();
                });
        status.is_ok()) {
      any_successes = true;
    } else {
      failures.emplace_back(std::move(status));
    }
  }
  if (!any_successes) {
    for (const auto& status : failures) {
      OVERNET_TRACE(WARNING) << "Omdp broadcast failed: " << status;
    }
  }
  if_freenameindex(interfaces);
}

void OmdpNub::OnNewNode(uint64_t node_id, IpAddr addr) {
  udp_nub_->Initiate({addr}, NodeId(node_id));
}

void OmdpNub::WaitForInbound() {
  reactor_->OnRead(incoming_.get(),
                   [this](const Status& status) { InboundReady(status); });
}

void OmdpNub::InboundReady(const Status& status) {
  OVERNET_TRACE(DEBUG) << "InboundReady gets status: " << status;
  if (status.is_error()) {
    return;
  }
  auto data_and_addr = incoming_.RecvFrom(kMaximumPacketSize, 0);
  if (data_and_addr.is_error()) {
    OVERNET_TRACE(ERROR) << data_and_addr.AsStatus();
    // Wait a bit before trying again to avoid spamming the log.
    timer_->At(timer_->Now() + TimeDelta::FromSeconds(10),
               [this] { WaitForInbound(); });
    return;
  }

  ScopedOp scoped_op(Op::New(overnet::OpType::INCOMING_PACKET));
  OVERNET_TRACE(TRACE) << "Got omdp packet " << data_and_addr->data << " from "
                       << data_and_addr->addr;
  if (auto status = Process(std::move(data_and_addr->addr),
                            std::move(data_and_addr->data));
      status.is_error()) {
    OVERNET_TRACE(ERROR) << "Omdp process failed: " << status;
  }
  WaitForInbound();
}

}  // namespace overnet
