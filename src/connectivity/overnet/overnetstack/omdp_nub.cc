// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/overnet/overnetstack/omdp_nub.h"

#include "garnet/public/lib/fostr/fidl/fuchsia/overnet/omdp/formatting.h"
#include "src/connectivity/overnet/lib/omdp/omdp.h"
#include "src/connectivity/overnet/lib/vocabulary/ip_addr.h"

namespace overnetstack {

std::random_device rng_dev;
static const size_t kMaximumPacketSize = 1400;

OmdpNub::OmdpNub(OvernetApp* app, UdpNub* udp_nub)
    : Omdp(app->node_id().get(), app->timer(), []() { return rng_dev(); }),
      udp_nub_(udp_nub) {}

OmdpNub::~OmdpNub() = default;

overnet::Status OmdpNub::Start() {
  EnsureIncoming();
  ScheduleBroadcast();
  return overnet::Status::Ok();
}

void OmdpNub::EnsureIncoming() {
  if (incoming_.IsValid()) {
    return;
  }
  auto status =
      incoming_.Create(AF_INET6, SOCK_DGRAM, 0)
          .Then([&] { return incoming_.SetOptReusePort(true); })
          .Then([&] {
            return incoming_.Bind(*overnet::IpAddr::AnyIpv6().WithPort(
                kMulticastGroupAddr.port()));
          })
          .Then([&] {
            return incoming_
                .SetOpt(IPPROTO_IPV6, IPV6_JOIN_GROUP,
                        ipv6_mreq{kMulticastGroupAddr.ipv6.sin6_addr, 0})
                .WithLazyContext([&] {
                  std::ostringstream out;
                  out << "Joining IPV6 multicast group "
                      << kMulticastGroupAddr.WithPort(0);
                  return out.str();
                });
          })
          .Then([&] {
            WaitForInbound();
            return overnet::Status::Ok();
          });
  if (status.is_error()) {
    OVERNET_TRACE(ERROR) << "Setting up OMDP receiver: " << status;
    incoming_.Close();
  }
}

void OmdpNub::OnNewNode(uint64_t node_id, overnet::IpAddr addr) {
  udp_nub_->Initiate({addr}, overnet::NodeId(node_id));
}

void OmdpNub::Broadcast(overnet::Slice data) {
  EnsureIncoming();
  OVERNET_TRACE(DEBUG) << "BROADCAST " << data << " to " << kMulticastGroupAddr;
  if (auto status =
          udp_nub_->socket()->SendTo(std::move(data), 0, kMulticastGroupAddr);
      status.is_error()) {
    OVERNET_TRACE(WARNING) << "Omdp broadcast failed: " << status;
  }
}

void OmdpNub::WaitForInbound() {
  if (!fd_waiter_.Wait(
          [this](zx_status_t status, uint32_t events) {
            InboundReady(status, events);
          },
          incoming_.get(), POLLIN | POLLERR)) {
    OVERNET_TRACE(DEBUG) << "fd_waiter_.Wait() failed\n";
  }
}

void OmdpNub::InboundReady(zx_status_t status, uint32_t events) {
  auto data_and_addr = incoming_.RecvFrom(kMaximumPacketSize, 0);
  if (data_and_addr.is_error()) {
    OVERNET_TRACE(ERROR) << data_and_addr.AsStatus();
    // Wait a bit before trying again to avoid spamming the log.
    async::PostDelayedTask(
        async_get_default_dispatcher(), [this]() { WaitForInbound(); },
        zx::sec(10));
    return;
  }

  overnet::ScopedOp scoped_op(
      overnet::Op::New(overnet::OpType::INCOMING_PACKET));
  OVERNET_TRACE(TRACE) << "Got omdp packet " << data_and_addr->data << " from "
                       << data_and_addr->addr;
  if (auto status = Process(std::move(data_and_addr->addr),
                            std::move(data_and_addr->data));
      status.is_error()) {
    OVERNET_TRACE(ERROR) << "Omdp process failed: " << status;
  }
  WaitForInbound();
}

}  // namespace overnetstack
