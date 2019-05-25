// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_OVERNET_OVERNETSTACK_UDP_NUB_H_
#define SRC_CONNECTIVITY_OVERNET_OVERNETSTACK_UDP_NUB_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/fsl/tasks/fd_waiter.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>

#include "src/connectivity/overnet/lib/links/packet_nub.h"
#include "src/connectivity/overnet/lib/vocabulary/ip_addr.h"
#include "src/connectivity/overnet/lib/vocabulary/socket.h"
#include "src/connectivity/overnet/overnetstack/overnet_app.h"

namespace overnetstack {

static constexpr uint32_t kAssumedUDPPacketSize = 1500;

using UdpNubBase = overnet::PacketNub<overnet::IpAddr, kAssumedUDPPacketSize,
                                      overnet::HashIpAddr, overnet::EqIpAddr>;

class UdpNub final : public UdpNubBase, public OvernetApp::Actor {
 public:
  explicit UdpNub(OvernetApp* app)
      : UdpNubBase(app->endpoint()),
        endpoint_(app->endpoint()),
        timer_(app->timer()) {}

  overnet::Status Start() override {
    return CreateFD()
        .Then([this]() { return SetOptionSharePort(); })
        .Then([this]() { return SetOptionReceiveAnything(); })
        .Then([this]() { return Bind(); })
        .Then([this]() {
          WaitForInbound();
          return overnet::Status::Ok();
        });
  }

  uint16_t port() { return port_; }

  overnet::NodeId node_id() { return endpoint_->node_id(); }

  void SendTo(overnet::IpAddr addr, overnet::Slice slice) override {
    OVERNET_TRACE(TRACE) << "sending packet " << slice << " to " << addr;
    if (auto status = socket_.SendTo(std::move(slice), 0, *addr.AsIpv6());
        status.is_error()) {
      OVERNET_TRACE(WARNING) << "sendto fails: " << status;
    }
  }

  void Publish(overnet::LinkPtr<> link) override {
    overnet::NodeId node = overnet::NodeId(link->GetLinkStatus().to);
    OVERNET_TRACE(DEBUG) << "NewLink: " << node << "\n";
    endpoint_->RegisterPeer(node);
    endpoint_->RegisterLink(std::move(link));
  }

  overnet::Socket* socket() { return &socket_; }

 private:
  overnet::RouterEndpoint* const endpoint_;
  overnet::Timer* const timer_;
  overnet::Socket socket_;
  uint16_t port_ = -1;
  fsl::FDWaiter fd_waiter_;

  void WaitForInbound() {
    assert(socket_.IsValid());
    if (!fd_waiter_.Wait(
            [this](zx_status_t status, uint32_t events) {
              InboundReady(status, events);
            },
            socket_.get(), POLLIN)) {
      OVERNET_TRACE(DEBUG) << "fd_waiter_.Wait() failed\n";
    }
  }

  void InboundReady(zx_status_t status, uint32_t events) {
    auto now = timer_->Now();

    auto data_and_addr = socket_.RecvFrom(kAssumedUDPPacketSize, 0);
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
    OVERNET_TRACE(TRACE) << "Got packet " << data_and_addr->data << " from "
                         << data_and_addr->addr;
    Process(now, data_and_addr->addr, std::move(data_and_addr->data));

    WaitForInbound();
  }

  overnet::Status CreateFD() {
    socket_ = overnet::Socket(::socket(AF_INET6, SOCK_DGRAM, 0));
    if (!socket_.IsValid()) {
      return StatusFromErrno("Failed to create socket");
    }
    return overnet::Status::Ok();
  }

  overnet::Status SetOptionSharePort() { return socket_.SetOptReusePort(true); }

  overnet::Status SetOptionReceiveAnything() {
    return overnet::Status::Ok();
    // return SetSockOpt(IPPROTO_IPV6, IPV6_V6ONLY, 0, "IPV6_ONLY");
  }

  overnet::Status Bind() {
    sockaddr_in6 addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin6_family = AF_INET6;
    addr.sin6_addr = in6addr_any;

    int result =
        bind(socket_.get(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    if (result < 0) {
      return StatusFromErrno("Failed to bind() to in6addr_any");
    }

    socklen_t len = sizeof(addr);
    result =
        getsockname(socket_.get(), reinterpret_cast<sockaddr*>(&addr), &len);
    if (result < 0) {
      return StatusFromErrno("Failed to getsockname() for new socket");
    }
    port_ = ntohs(addr.sin6_port);

    return overnet::Status::Ok();
  }

  overnet::Status StatusFromErrno(const std::string& why) {
    int err = errno;
    std::ostringstream msg;
    msg << why << ", errno=" << err;
    // TODO(ctiller): Choose an appropriate status code based upon errno?
    return overnet::Status(overnet::StatusCode::UNKNOWN, msg.str());
  }
};

}  // namespace overnetstack

#endif  // SRC_CONNECTIVITY_OVERNET_OVERNETSTACK_UDP_NUB_H_
