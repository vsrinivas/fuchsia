// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @file
 *   This file implements the infrastructure interface for fuchsia.
 */

#include "infra_if.h"
#if OPENTHREAD_CONFIG_BORDER_ROUTING_ENABLE

#include <errno.h>
#include <ifaddrs.h>
#include <lib/syslog/cpp/macros.h>
#include <net/if.h>
#include <netinet/icmp6.h>
#include <netinet/in.h>
#include <netinet/ip6.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <zircon/status.h>

#include <openthread/border_router.h>
#include <openthread/platform/infra_if.h>

#include "common/code_utils.hpp"
#include "common/debug.hpp"

bool otPlatInfraIfHasAddress(uint32_t a_infra_if_index, const otIp6Address *a_address) {
  bool ret = false;
  struct ifaddrs *if_addrs = nullptr;

  VERIFY_OR_ASSERT(getifaddrs(&if_addrs) != -1, OT_EXIT_ERROR_ERRNO);

  for (struct ifaddrs *addr = if_addrs; addr != nullptr; addr = addr->ifa_next) {
    struct sockaddr_in6 *ip6Addr;

    if (if_nametoindex(addr->ifa_name) != a_infra_if_index ||
        addr->ifa_addr->sa_family != AF_INET6) {
      continue;
    }

    ip6Addr = reinterpret_cast<sockaddr_in6 *>(addr->ifa_addr);
    if (memcmp(&ip6Addr->sin6_addr, a_address, sizeof(*a_address)) == 0) {
      ExitNow(ret = true);
    }
  }

exit:
  freeifaddrs(if_addrs);
  return ret;
}

otError otPlatInfraIfSendIcmp6Nd(uint32_t a_infra_if_index, const otIp6Address *a_dest_address,
                                 const uint8_t *a_buffer, uint16_t a_buffer_length) {
  return ot::Fuchsia::InfraNetif::Get().SendIcmp6Nd(a_infra_if_index, *a_dest_address, a_buffer,
                                                    a_buffer_length);
}

extern "C" {
void platformInfraIfOnReceiveIcmp6Msg(otInstance *a_instance) {
  ot::Fuchsia::InfraNetif::Get().ReceiveIcmp6Message(a_instance);
}

int platformInfraIfInit(int infra_if_idx) {
  ot::Fuchsia::InfraNetif::Get().Init(infra_if_idx);
  return ot::Fuchsia::InfraNetif::Get().GetIcmpSocket();
}

void platformInfraIfOnStateChanged(otInstance *a_instance) {
  ot::Fuchsia::InfraNetif::Get().OnStateChanged(a_instance);
}
}
bool platformInfraIfIsRunning(void) { return ot::Fuchsia::InfraNetif::Get().IsRunning(); }

namespace ot {
namespace Fuchsia {
namespace {

int SocketWithCloseExec(int a_domain, int a_type, int a_protocol,
                        SocketBlockOption a_block_option) {
  int rval = 0;
  int fd = -1;

  a_type |= a_block_option == kSocketNonBlock ? SOCK_CLOEXEC | SOCK_NONBLOCK : SOCK_CLOEXEC;
  VerifyOrExit((fd = socket(a_domain, a_type, a_protocol)) != -1, perror("socket(SOCK_CLOEXEC)"));

exit:
  if (rval == -1) {
    VERIFY_OR_ASSERT(close(fd) == 0, "close(fd) failed");
    fd = -1;
  }

  return fd;
}

int CreateIcmp6Socket(void) {
  int sock;
  int rval;
  struct icmp6_filter filter;
  // const int kEnable = 1;
  // const int kIpv6ChecksumOffset = 2;
  const int kHopLimit = 255;

  // Initializes the ICMPv6 socket.
  sock = SocketWithCloseExec(AF_INET6, SOCK_RAW, IPPROTO_ICMPV6, kSocketNonBlock);
  VERIFY_OR_ASSERT(sock != -1, OT_EXIT_ERROR_ERRNO);

  // Only accept router advertisements and solicitations.
  ICMP6_FILTER_SETBLOCKALL(&filter);
  ICMP6_FILTER_SETPASS(ND_ROUTER_SOLICIT, &filter);
  ICMP6_FILTER_SETPASS(ND_ROUTER_ADVERT, &filter);

  rval = setsockopt(sock, IPPROTO_ICMPV6, ICMP6_FILTER, &filter, sizeof(filter));
  otPlatLog(OT_LOG_LEVEL_WARN, OT_LOG_REGION_PLATFORM,
            "IPPROTO_ICMPV6, ICMP6_FILTER ret_val:%lld, errno:%s", rval, strerror(errno));
  VERIFY_OR_ASSERT(rval == 0, OT_EXIT_ERROR_ERRNO);

  // TODO(fxbug.dev/52565): Re-enable once we have IPV6_RECVPKTINFO
  // We want a source address and interface index.
  // rval = setsockopt(sock, IPPROTO_IPV6, IPV6_RECVPKTINFO, &kEnable, sizeof(kEnable));
  // otPlatLog(OT_LOG_LEVEL_WARN, OT_LOG_REGION_PLATFORM, "IPPROTO_IPV6, IPV6_RECVPKTINFO
  // ret_val:%lld, errno:%s", rval, strerror(errno)); VERIFY_OR_ASSERT(rval == 0,
  // OT_EXIT_ERROR_ERRNO);

  // TODO(fxbug.dev/82535): re-enable once we have IPV6_RECVHOPLIMIT
  // We need to be able to reject RAs arriving from off-link.
  // rval = setsockopt(sock, IPPROTO_IPV6, IPV6_RECVHOPLIMIT, &kEnable, sizeof(kEnable));
  // otPlatLog(OT_LOG_LEVEL_WARN, OT_LOG_REGION_PLATFORM, "IPPROTO_IPV6, IPV6_RECVHOPLIMIT
  // ret_val:%lld, errno:%s", rval, strerror(errno)); VERIFY_OR_ASSERT(rval == 0,
  // OT_EXIT_ERROR_ERRNO);

  rval = setsockopt(sock, IPPROTO_IPV6, IPV6_UNICAST_HOPS, &kHopLimit, sizeof(kHopLimit));
  otPlatLog(OT_LOG_LEVEL_WARN, OT_LOG_REGION_PLATFORM,
            "IPPROTO_IPV6, IPV6_UNICAST_HOPS ret_val:%lld, errno:%s", rval, strerror(errno));
  VERIFY_OR_ASSERT(rval == 0, OT_EXIT_ERROR_ERRNO);

  rval = setsockopt(sock, IPPROTO_IPV6, IPV6_MULTICAST_HOPS, &kHopLimit, sizeof(kHopLimit));
  otPlatLog(OT_LOG_LEVEL_WARN, OT_LOG_REGION_PLATFORM,
            "IPPROTO_IPV6, IPV6_MULTICAST_HOPS ret_val:%lld, errno:%s", rval, strerror(errno));
  VERIFY_OR_ASSERT(rval == 0, OT_EXIT_ERROR_ERRNO);

  return sock;
}

}  // namespace

bool InfraNetif::IsRunning(void) const {
  int sock;
  struct ifreq if_req;

  OT_ASSERT(infra_if_idx_ != 0);

  sock = SocketWithCloseExec(AF_INET6, SOCK_DGRAM, IPPROTO_IP, kSocketBlock);
  VERIFY_OR_ASSERT(sock != -1, OT_EXIT_ERROR_ERRNO);

  memset(&if_req, 0, sizeof(if_req));
  VERIFY_OR_ASSERT(sizeof(if_req.ifr_name) >= sizeof(infra_if_name_),
                   "infra_if name longer than expected");
  strcpy(if_req.ifr_name, infra_if_name_);

  VERIFY_OR_ASSERT(ioctl(sock, SIOCGIFFLAGS, &if_req) != -1, OT_EXIT_ERROR_ERRNO);

  close(sock);

  return (if_req.ifr_flags & IFF_RUNNING) && HasLinkLocalAddress();
}

bool InfraNetif::HasLinkLocalAddress(void) const {
  bool has_lla = false;
  struct ifaddrs *if_addrs = nullptr;

  VERIFY_OR_ASSERT(getifaddrs(&if_addrs) >= 0, strerror(errno));

  for (struct ifaddrs *addr = if_addrs; addr != nullptr; addr = addr->ifa_next) {
    struct sockaddr_in6 *ip6Addr;

    if (strncmp(addr->ifa_name, infra_if_name_, strlen(infra_if_name_)) != 0 ||
        addr->ifa_addr->sa_family != AF_INET6) {
      continue;
    }

    ip6Addr = reinterpret_cast<sockaddr_in6 *>(addr->ifa_addr);
    if (IN6_IS_ADDR_LINKLOCAL(&ip6Addr->sin6_addr)) {
      has_lla = true;
      break;
    }
  }

  freeifaddrs(if_addrs);
  return has_lla;
}

otError InfraNetif::SendIcmp6Nd(uint32_t a_infra_if_index, const otIp6Address &a_dest_address,
                                const uint8_t *a_buffer, uint16_t a_buffer_length) {
  otError error = OT_ERROR_NONE;

  struct iovec iov;
  struct msghdr msgHeader;
  ssize_t rval;
  struct sockaddr_in6 dest;

  VerifyOrExit(infra_if_icmp6_socket_ >= 0, error = OT_ERROR_FAILED);
  VerifyOrExit(a_infra_if_index == infra_if_idx_, error = OT_ERROR_DROP);

  // Send the message
  memset(&dest, 0, sizeof(dest));
  dest.sin6_family = AF_INET6;
  memcpy(&dest.sin6_addr, &a_dest_address, sizeof(a_dest_address));
  if (IN6_IS_ADDR_LINKLOCAL(&dest.sin6_addr) || IN6_IS_ADDR_MC_LINKLOCAL(&dest.sin6_addr)) {
    dest.sin6_scope_id = infra_if_idx_;
  }

  iov.iov_base = const_cast<uint8_t *>(a_buffer);
  iov.iov_len = a_buffer_length;

  msgHeader.msg_namelen = sizeof(dest);
  msgHeader.msg_name = &dest;
  msgHeader.msg_iov = &iov;
  msgHeader.msg_iovlen = 1;
  msgHeader.msg_control = nullptr;
  msgHeader.msg_controllen = 0;

  rval = sendmsg(infra_if_icmp6_socket_, &msgHeader, 0);
  if (rval < 0) {
    otPlatLog(OT_LOG_LEVEL_WARN, OT_LOG_REGION_PLATFORM, "failed to send ICMPv6 message: %s",
              strerror(errno));
    ExitNow(error = OT_ERROR_FAILED);
  }

  if (static_cast<size_t>(rval) != iov.iov_len) {
    otPlatLog(OT_LOG_LEVEL_WARN, OT_LOG_REGION_PLATFORM,
              "failed to send ICMPv6 message: partially sent");
    ExitNow(error = OT_ERROR_FAILED);
  }

exit:
  return error;
}

void InfraNetif::Init(uint32_t infra_if_idx) {
  ssize_t rval;

  // Initializes the infra interface.
  VERIFY_OR_ASSERT(infra_if_idx != 0, OT_EXIT_INVALID_ARGUMENTS);
  infra_if_idx_ = infra_if_idx;

  // Get and store the interface name
  struct ifaddrs *if_addrs = nullptr;

  VERIFY_OR_ASSERT(getifaddrs(&if_addrs) != -1, OT_EXIT_ERROR_ERRNO);
  VERIFY_OR_ASSERT(infra_if_name_len_ == 0, "InfraNetif::Init() memory corrputed");

  if_indextoname(infra_if_idx_, infra_if_name_);
  otPlatLog(OT_LOG_LEVEL_INFO, OT_LOG_REGION_PLATFORM,
            "InfraNetif::Init() intf found: infra_if_name_:%s, strlen:%u, size:%u", infra_if_name_,
            strlen(infra_if_name_), sizeof(*(infra_if_name_)));
  infra_if_name_len_ = strlen(infra_if_name_);

  otPlatLog(OT_LOG_LEVEL_INFO, OT_LOG_REGION_PLATFORM, "InfraNetif::Init() infra_if_name_len_:%llu",
            infra_if_name_len_);
  VERIFY_OR_ASSERT(infra_if_name_len_ != 0,
                   "InfraNetif::Init() infra if name not found by if_nametoindex()");

  freeifaddrs(if_addrs);

  infra_if_icmp6_socket_ = CreateIcmp6Socket();
  rval = setsockopt(infra_if_icmp6_socket_, SOL_SOCKET, SO_BINDTODEVICE, infra_if_name_,
                    strlen(infra_if_name_));
  otPlatLog(OT_LOG_LEVEL_INFO, OT_LOG_REGION_PLATFORM,
            "InfraNetif::Init() SOL_SOCKET, SO_BINDTODEVICE rval:%d, errno:%s", rval,
            strerror(errno));
  VERIFY_OR_ASSERT(rval == 0, OT_EXIT_ERROR_ERRNO);
  otPlatLog(OT_LOG_LEVEL_INFO, OT_LOG_REGION_PLATFORM,
            "InfraNetif::Init() returned, infra_if_name_:%s", infra_if_name_);
}

void InfraNetif::Deinit(void) {
  if (infra_if_icmp6_socket_ != -1) {
    close(infra_if_icmp6_socket_);
    infra_if_icmp6_socket_ = -1;
  }

  infra_if_idx_ = 0;
}

void InfraNetif::OnStateChanged(otInstance *a_instance) {
  VERIFY_OR_ASSERT(OT_ERROR_NONE == otPlatInfraIfStateChanged(a_instance, infra_if_idx_,
                                                              platformInfraIfIsRunning()),
                   "otPlatInfraIfStateChanged() failed");
}

void InfraNetif::ReceiveIcmp6Message(otInstance *a_instance) {
  otError error = OT_ERROR_NONE;
  uint8_t buffer[1500];
  uint16_t bufferLength;

  ssize_t rval;
  struct msghdr msg;
  struct iovec bufp;
  char cmsgbuf[128];
  struct cmsghdr *cmh;
  // uint32_t ifIndex = 0;
  // int hopLimit = -1;

  struct sockaddr_in6 srcAddr;
  struct in6_addr dstAddr;

  memset(&srcAddr, 0, sizeof(srcAddr));
  memset(&dstAddr, 0, sizeof(dstAddr));

  bufp.iov_base = buffer;
  bufp.iov_len = sizeof(buffer);
  msg.msg_iov = &bufp;
  msg.msg_iovlen = 1;
  msg.msg_name = &srcAddr;
  msg.msg_namelen = sizeof(srcAddr);
  msg.msg_control = cmsgbuf;
  msg.msg_controllen = sizeof(cmsgbuf);

  rval = recvmsg(infra_if_icmp6_socket_, &msg, 0);
  if (rval < 0) {
    ExitNow(error = OT_ERROR_DROP);
  }
  bufferLength = static_cast<uint16_t>(rval);

  // TODO(fxbug.dev/52565): Re-enable once we have IPV6_RECVPKTINFO
  for (cmh = CMSG_FIRSTHDR(&msg); cmh; cmh = CMSG_NXTHDR(&msg, cmh)) {
    // if (cmh->cmsg_level == IPPROTO_IPV6 && cmh->cmsg_type == IPV6_PKTINFO &&
    //     cmh->cmsg_len == CMSG_LEN(sizeof(struct in6_pktinfo))) {
    //   struct in6_pktinfo pktinfo;

    //   memcpy(&pktinfo, CMSG_DATA(cmh), sizeof pktinfo);
    //   ifIndex = pktinfo.ipi6_ifindex;
    //   dstAddr = pktinfo.ipi6_addr;
    // } else
    // if (cmh->cmsg_level == IPPROTO_IPV6 && cmh->cmsg_type == IPV6_HOPLIMIT &&
    //            cmh->cmsg_len == CMSG_LEN(sizeof(int))) {
    //   hopLimit = *(int *)CMSG_DATA(cmh);
    // }
  }

  // VerifyOrExit(ifIndex == infra_if_idx_, error = OT_ERROR_DROP);

  // We currently accept only RA & RS messages for the Border Router and it requires that
  // the hoplimit must be 255 and the source address must be a link-local address.
  // VerifyOrExit(hopLimit == 255 && IN6_IS_ADDR_LINKLOCAL(&srcAddr.sin6_addr), error =
  // OT_ERROR_DROP);

  if (!IN6_IS_ADDR_LINKLOCAL(&srcAddr.sin6_addr)) {
    otPlatLog(OT_LOG_LEVEL_CRIT, OT_LOG_REGION_PLATFORM,
              "receving non link-local address in infra_if, ignoring the message");
    return;
  }

  otPlatLog(OT_LOG_LEVEL_DEBG, OT_LOG_REGION_PLATFORM,
            "InfraNetif::ReceiveIcmp6Message() calling otPlatInfraIfRecvIcmp6Nd()\
    a_instance:x%llx, infra_if_idx_:%llu, src_addr:[%.2x%.2x:%.2x%.2x,..], buffer:[%u,%u,%u,%u,..], buf_len:%u",
            reinterpret_cast<uint64_t>(a_instance), infra_if_idx_,
            reinterpret_cast<uint8_t *>(&srcAddr.sin6_addr)[0],
            reinterpret_cast<uint8_t *>(&srcAddr.sin6_addr)[1],
            reinterpret_cast<uint8_t *>(&srcAddr.sin6_addr)[2],
            reinterpret_cast<uint8_t *>(&srcAddr.sin6_addr)[3], buffer[0], buffer[1], buffer[2],
            buffer[3], bufferLength);
  otPlatInfraIfRecvIcmp6Nd(a_instance, infra_if_idx_,
                           reinterpret_cast<otIp6Address *>(&srcAddr.sin6_addr), buffer,
                           bufferLength);

exit:
  if (error != OT_ERROR_NONE) {
    otPlatLog(OT_LOG_LEVEL_DEBG, OT_LOG_REGION_PLATFORM, "failed to handle ICMPv6 message: %s",
              otThreadErrorToString(error));
  }
}

InfraNetif &InfraNetif::Get(void) {
  static InfraNetif sInstance;
  return sInstance;
}

}  // namespace Fuchsia
}  // namespace ot
#endif  // OPENTHREAD_CONFIG_BORDER_ROUTING_ENABLE
