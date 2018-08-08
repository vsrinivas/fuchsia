// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <lib/async-loop/cpp/loop.h>
#include "garnet/lib/overnet/packet_nub.h"
#include "lib/fsl/tasks/fd_waiter.h"
#include "lib/fxl/files/unique_fd.h"

namespace overnetstack {

union UdpAddr {
  sockaddr_in ipv4;
  sockaddr_in6 ipv6;
  sockaddr addr;
};

inline std::ostream& operator<<(std::ostream& out, UdpAddr addr) {
  char dst[512];
  switch (addr.addr.sa_family) {
    case AF_INET:
      inet_ntop(AF_INET, &addr.ipv4.sin_addr, dst, sizeof(dst));
      return out << dst << ":" << ntohs(addr.ipv4.sin_port);
    case AF_INET6:
      inet_ntop(AF_INET6, &addr.ipv6.sin6_addr, dst, sizeof(dst));
      return out << dst << ":" << ntohs(addr.ipv6.sin6_port);
    default:
      return out << "<<unknown address family " << addr.addr.sa_family << ">>";
  }
}

class HashUdpAddr {
 public:
  size_t operator()(const UdpAddr& addr) const {
    size_t out = 0;
    auto add_value = [&out](auto x) {
      const char* p = reinterpret_cast<const char*>(&x);
      const char* end = reinterpret_cast<const char*>(1 + &x);
      while (p != end) {
        out = 257 * out + *p++;
      }
    };
    switch (addr.addr.sa_family) {
      case AF_INET:
        add_value(addr.ipv4.sin_addr);
        add_value(addr.ipv4.sin_port);
        break;
      case AF_INET6:
        add_value(addr.ipv6.sin6_addr);
        add_value(addr.ipv6.sin6_port);
        break;
    }
    return out;
  }
};

class EqUdpAddr {
 public:
  bool operator()(const UdpAddr& a, const UdpAddr& b) const {
    if (a.addr.sa_family == b.addr.sa_family) {
      switch (a.addr.sa_family) {
        case AF_INET:
          return a.ipv4.sin_port == b.ipv4.sin_port &&
                 0 == memcmp(&a.ipv4.sin_addr, &b.ipv4.sin_addr,
                             sizeof(a.ipv4.sin_addr));
        case AF_INET6:
          return a.ipv6.sin6_port == b.ipv6.sin6_port &&
                 0 == memcmp(&a.ipv6.sin6_addr, &b.ipv6.sin6_addr,
                             sizeof(a.ipv6.sin6_addr));
      }
    }
    return false;
  }
};

using UdpNubBase = overnet::PacketNub<UdpAddr, 1500, HashUdpAddr, EqUdpAddr>;

class UdpNub final : public UdpNubBase {
 public:
  explicit UdpNub(overnet::RouterEndpoint* endpoint)
      : UdpNubBase(endpoint->router()->timer(), endpoint->node_id()),
        endpoint_(endpoint),
        timer_(endpoint->router()->timer()) {}

  overnet::Status Start() {
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

  void SendTo(UdpAddr addr, overnet::Slice slice) override {
    if (addr.addr.sa_family == AF_INET) {
      // Convert ipv4 to ipv6 address.
      UdpAddr addr6;
      memset(&addr6, 0, sizeof(addr6));
      addr6.ipv6.sin6_family = AF_INET6;
      addr6.ipv6.sin6_port = addr.ipv4.sin_port;
      uint8_t* addr6_addr_bytes =
          reinterpret_cast<uint8_t*>(&addr6.ipv6.sin6_addr);
      addr6_addr_bytes[10] = 0xff;
      addr6_addr_bytes[11] = 0xff;
      memcpy(addr6_addr_bytes + 12, &addr.ipv4.sin_addr, 4);
      addr = addr6;
    }
    std::cout << "sending packet length " << slice.length() << " to " << addr
              << "\n";
    int r = sendto(socket_fd_.get(), slice.begin(), slice.length(), 0,
                   &addr.addr, sizeof(addr));
    if (r == -1) {
      auto got_errno = errno;
      std::cout << "sendto sets errno " << got_errno << "\n";
    }
    assert(static_cast<size_t>(r) == slice.length());
  }

  overnet::Router* GetRouter() override { return endpoint_->router(); }

  void Publish(std::unique_ptr<overnet::Link> link) override {
    overnet ::NodeId node = link->GetLinkMetrics().to();
    std::cout << "NewLink: " << node << "\n";
    endpoint_->RegisterPeer(node);
    endpoint_->router()->RegisterLink(std::move(link));
  }

 private:
  overnet::RouterEndpoint* const endpoint_;
  overnet::Timer* const timer_;
  fxl::UniqueFD socket_fd_;
  uint16_t port_ = -1;
  fsl::FDWaiter fd_waiter_;

  void WaitForInbound() {
    assert(socket_fd_.is_valid());
    UdpAddr whoami;
    socklen_t whoami_len = sizeof(whoami.addr);
    if (getsockname(socket_fd_.get(), &whoami.addr, &whoami_len) < 0) {
      std::cerr << StatusFromErrno("getsockname") << "\n";
    }
    std::cerr << "WaitForInbound on " << whoami << "\n";
    if (!fd_waiter_.Wait(
            [this](zx_status_t status, uint32_t events) {
              InboundReady(status, events);
            },
            socket_fd_.get(), POLLIN)) {
      std::cerr << "fd_waiter_.Wait() failed\n";
    }
  }

  void InboundReady(zx_status_t status, uint32_t events) {
    auto now = timer_->Now();

    UdpAddr source_address;
    socklen_t source_address_length = sizeof(source_address);
    auto inbound = NewInboundSlice(1500);
    ssize_t result = recvfrom(
        socket_fd_.get(), const_cast<uint8_t*>(inbound.begin()),
        inbound.length(), 0, &source_address.addr, &source_address_length);
    if (result < 0) {
      FXL_LOG(ERROR) << "Failed to recvfrom, errno " << errno;
      // Wait a bit before trying again to avoid spamming the log.
      async::PostDelayedTask(async_get_default_dispatcher(),
                             [this]() { WaitForInbound(); }, zx::sec(10));
      return;
    }

    inbound.TrimEnd(inbound.length() - result);
    assert(inbound.length() == (size_t)result);
    std::cerr << "Got packet length " << result << "\n";
    Process(now, source_address, std::move(inbound));

    WaitForInbound();
  }

  overnet::Status CreateFD() {
    socket_fd_ = fxl::UniqueFD(socket(AF_INET6, SOCK_DGRAM, 0));
    if (!socket_fd_.is_valid()) {
      return StatusFromErrno("Failed to create socket");
    }
    return overnet::Status::Ok();
  }

  overnet::Status SetOptionSharePort() {
    return SetSockOpt(SOL_SOCKET, SO_REUSEADDR, 1, "SO_REUSEADDR");
  }

  overnet::Status SetOptionReceiveAnything() {
    return overnet::Status::Ok();
    // return SetSockOpt(IPPROTO_IPV6, IPV6_V6ONLY, 0, "IPV6_ONLY");
  }

  overnet::Status Bind() {
    sockaddr_in6 addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin6_family = AF_INET6;
    addr.sin6_addr = in6addr_any;

    int result = bind(socket_fd_.get(), reinterpret_cast<sockaddr*>(&addr),
                      sizeof(addr));
    if (result < 0) {
      return StatusFromErrno("Failed to bind() to in6addr_any");
    }

    socklen_t len = sizeof(addr);
    result =
        getsockname(socket_fd_.get(), reinterpret_cast<sockaddr*>(&addr), &len);
    if (result < 0) {
      return StatusFromErrno("Failed to getsockname() for new socket");
    }
    port_ = ntohs(addr.sin6_port);

    return overnet::Status::Ok();
  }

  overnet::Status SetSockOpt(int family, int opt, int param, const char* name) {
    int result =
        setsockopt(socket_fd_.get(), family, opt, &param, sizeof(param));
    if (result < 0) {
      return StatusFromErrno(std::string("Failed to set socket option ") +
                             name);
    }
    return overnet::Status::Ok();
  }

  overnet::Status StatusFromErrno(const std::string& why) {
    int err = errno;
    std::ostringstream msg;
    msg << why << ", errno=" << err;
    // TODO(ctiller): Choose an appropriate status code based upon errno?
    return overnet::Status(overnet::StatusCode::UNKNOWN, msg.str());
  }

  overnet::Slice NewInboundSlice(size_t size) {
    return overnet::Slice::WithInitializer(size, [](uint8_t*) {});
  }
};

}  // namespace overnetstack
