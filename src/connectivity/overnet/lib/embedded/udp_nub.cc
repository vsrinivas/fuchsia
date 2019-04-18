// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/overnet/lib/embedded/udp_nub.h"
#include <sys/socket.h>
#include <unistd.h>

namespace overnet {

UdpNub::UdpNub(OvernetEmbedded* root)
    : OvernetEmbedded::Actor(root),
      UdpNubBase(root->timer(), root->node_id()),
      endpoint_(root->endpoint()),
      timer_(root->timer()),
      reactor_(root->reactor()) {}

UdpNub::~UdpNub() = default;

Status UdpNub::Start() {
  return CreateFD()
      .Then([this]() { return SetOptionSharePort(); })
      .Then([this]() { return SetOptionReceiveAnything(); })
      .Then([this]() { return Bind(); })
      .Then([this]() {
        WaitForInbound();
        return overnet::Status::Ok();
      });
}

void UdpNub::SendTo(IpAddr addr, overnet::Slice slice) {
  OVERNET_TRACE(TRACE) << "sending packet " << slice << " to " << addr;
  if (auto status = socket_.SendTo(std::move(slice), 0, *addr.AsIpv6());
      status.is_error()) {
    OVERNET_TRACE(WARNING) << "sendto fails: " << status;
  }
}

void UdpNub::WaitForInbound() {
  assert(socket_.IsValid());
  reactor_->OnRead(socket_.get(),
                   [this](const Status& status) { InboundReady(status); });
}

void UdpNub::InboundReady(const Status& status) {
  OVERNET_TRACE(DEBUG) << "UdpNub inbound ready gets " << status;
  if (status.is_error()) {
    return;
  }

  auto now = timer_->Now();

  auto data_and_addr = socket_.RecvFrom(1500, 0);
  if (data_and_addr.is_error()) {
    OVERNET_TRACE(ERROR) << data_and_addr.AsStatus();
    // Wait a bit before trying again to avoid spamming the log.
    timer_->At(timer_->Now() + TimeDelta::FromSeconds(10),
               [this] { WaitForInbound(); });
    return;
  }

  ScopedOp scoped_op(Op::New(overnet::OpType::INCOMING_PACKET));
  OVERNET_TRACE(TRACE) << "Got packet " << data_and_addr->data << " from "
                       << data_and_addr->addr;
  Process(now, data_and_addr->addr, std::move(data_and_addr->data));

  WaitForInbound();
}

Status UdpNub::CreateFD() {
  socket_ = Socket(::socket(AF_INET6, SOCK_DGRAM, 0));
  if (!socket_.IsValid()) {
    return StatusFromErrno("Failed to create socket");
  }
  return Status::Ok();
}

Status UdpNub::SetOptionSharePort() { return socket_.SetOptReusePort(true); }

Status UdpNub::SetOptionReceiveAnything() {
  return Status::Ok();
  // return SetSockOpt(IPPROTO_IPV6, IPV6_V6ONLY, 0, "IPV6_ONLY");
}

Status UdpNub::Bind() {
  if (auto status = socket_.Bind(IpAddr::AnyIpv6()); status.is_error()) {
    return status;
  }

  IpAddr addr;
  memset(&addr, 0, sizeof(addr));
  socklen_t len = sizeof(addr);
  const auto result = getsockname(socket_.get(), &addr.addr, &len);
  if (result < 0) {
    return StatusFromErrno("Failed to getsockname() for new socket");
  }
  if (addr.addr.sa_family != AF_INET6) {
    return Status(StatusCode::UNKNOWN, "Expected IPV6 address");
  }
  port_ = ntohs(addr.ipv6.sin6_port);

  return Status::Ok();
}

Status UdpNub::StatusFromErrno(const std::string& why) {
  int err = errno;
  std::ostringstream msg;
  msg << why << ", errno=" << err;
  // TODO(ctiller): Choose an appropriate status code based upon errno?
  return Status(overnet::StatusCode::UNKNOWN, msg.str());
}

}  // namespace overnet
