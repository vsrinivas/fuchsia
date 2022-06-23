// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <algorithm>

#include <fbl/unique_fd.h>
#include <gtest/gtest.h>

#include "constants.h"

namespace {

struct SendIpv6PacketInfoSuccessTestCase {
  std::string test_name;
  std::optional<std::string> send_local_addr_str;
  std::optional<std::string> send_local_if_str;
  std::string expected_recv_addr_str;
};

class SendIpv6PacketInfoSuccessTest
    : public testing::TestWithParam<SendIpv6PacketInfoSuccessTestCase> {};

TEST_P(SendIpv6PacketInfoSuccessTest, SendAndRecv) {
  fbl::unique_fd s;
  ASSERT_TRUE(s = fbl::unique_fd(socket(AF_INET6, SOCK_DGRAM, 0))) << strerror(errno);

  constexpr int kOne = 1;
  ASSERT_EQ(setsockopt(s.get(), SOL_IPV6, IPV6_RECVPKTINFO, &kOne, sizeof(kOne)), 0)
      << strerror(errno);

  const sockaddr_in6 bind_addr{
      .sin6_family = AF_INET6,
  };
  ASSERT_EQ(bind(s.get(), reinterpret_cast<const sockaddr*>(&bind_addr), sizeof(bind_addr)), 0)
      << strerror(errno);

  sockaddr_in6 to_addr = {
      .sin6_family = AF_INET6,
      .sin6_port = htons(kServerPort),
  };
  ASSERT_EQ(inet_pton(to_addr.sin6_family, kServerIpv6Addr, &to_addr.sin6_addr), 1);

  const auto [test_name, send_local_addr_str, send_local_if_str, expected_recv_addr_str] =
      GetParam();
  in6_addr expected_recv_addr;
  ASSERT_EQ(inet_pton(AF_INET6, expected_recv_addr_str.c_str(), &expected_recv_addr), 1);
  in6_pktinfo send_pktinfo = {};
  if (send_local_addr_str.has_value()) {
    ASSERT_EQ(inet_pton(AF_INET6, send_local_addr_str->c_str(), &send_pktinfo.ipi6_addr), 1);
  }
  if (send_local_if_str.has_value()) {
    send_pktinfo.ipi6_ifindex = if_nametoindex(send_local_if_str->c_str());
  }

  char send_buf[] = "hello";
  iovec send_iovec = {
      .iov_base = send_buf,
      .iov_len = sizeof(send_buf),
  };
  char send_control[CMSG_SPACE(sizeof(send_pktinfo)) + 1];
  msghdr send_msghdr = {
      .msg_name = &to_addr,
      .msg_namelen = sizeof(to_addr),
      .msg_iov = &send_iovec,
      .msg_iovlen = 1,
      .msg_control = send_control,
      .msg_controllen = sizeof(send_control),
  };
  cmsghdr* cmsg = CMSG_FIRSTHDR(&send_msghdr);
  ASSERT_NE(cmsg, nullptr);
  cmsg->cmsg_len = CMSG_LEN(sizeof(send_pktinfo));
  cmsg->cmsg_level = SOL_IPV6;
  cmsg->cmsg_type = IPV6_PKTINFO;
  memcpy(CMSG_DATA(cmsg), &send_pktinfo, sizeof(send_pktinfo));
  ASSERT_EQ(sendmsg(s.get(), &send_msghdr, 0), static_cast<ssize_t>(sizeof(send_buf)))
      << strerror(errno);

  constexpr char kExpectedRecvBuf[] = "Response: hello";
  in6_pktinfo recv_pktinfo;
  char recv_buf[sizeof(kExpectedRecvBuf) + 1];
  iovec recv_iovec = {
      .iov_base = recv_buf,
      .iov_len = sizeof(recv_buf),
  };
  char recv_control[CMSG_SPACE(sizeof(recv_pktinfo)) + 1];
  msghdr recv_msghdr = {
      .msg_iov = &recv_iovec,
      .msg_iovlen = 1,
      .msg_control = recv_control,
      .msg_controllen = sizeof(recv_control),
  };

  ASSERT_EQ(recvmsg(s.get(), &recv_msghdr, 0), static_cast<ssize_t>(sizeof(kExpectedRecvBuf)))
      << strerror(errno);
  EXPECT_EQ(std::string_view(kExpectedRecvBuf, sizeof(kExpectedRecvBuf)),
            std::string_view(recv_buf, sizeof(kExpectedRecvBuf)));
  cmsg = CMSG_FIRSTHDR(&recv_msghdr);
  ASSERT_NE(cmsg, nullptr);
  EXPECT_EQ(cmsg->cmsg_len, CMSG_LEN(sizeof(recv_pktinfo)));
  EXPECT_EQ(cmsg->cmsg_level, SOL_IPV6);
  EXPECT_EQ(cmsg->cmsg_type, IPV6_PKTINFO);
  memcpy(&recv_pktinfo, CMSG_DATA(cmsg), sizeof(recv_pktinfo));
  EXPECT_EQ(memcmp(&recv_pktinfo.ipi6_addr, &expected_recv_addr, sizeof(expected_recv_addr)), 0);
}

INSTANTIATE_TEST_SUITE_P(SocketTests, SendIpv6PacketInfoSuccessTest,
                         testing::Values(
                             SendIpv6PacketInfoSuccessTestCase{
                                 .test_name = "NIC1 local address",
                                 .send_local_addr_str = kClientIpv6Addr1,
                                 .expected_recv_addr_str = kClientIpv6Addr1,
                             },
                             SendIpv6PacketInfoSuccessTestCase{
                                 .test_name = "NIC1 local interface",
                                 .send_local_if_str = kClientNic1Name,
                                 .expected_recv_addr_str = kClientIpv6Addr1,
                             },
                             SendIpv6PacketInfoSuccessTestCase{
                                 .test_name = "NIC1 local address and interface",
                                 .send_local_addr_str = kClientIpv6Addr1,
                                 .send_local_if_str = kClientNic1Name,
                                 .expected_recv_addr_str = kClientIpv6Addr1,
                             },
                             SendIpv6PacketInfoSuccessTestCase{
                                 .test_name = "NIC2 local address",
                                 .send_local_addr_str = kClientIpv6Addr2,
                                 .expected_recv_addr_str = kClientIpv6Addr2,
                             },
                             SendIpv6PacketInfoSuccessTestCase{
                                 .test_name = "NIC2 local interface",
                                 .send_local_if_str = kClientNic2Name,
                                 .expected_recv_addr_str = kClientIpv6Addr2,
                             },
                             SendIpv6PacketInfoSuccessTestCase{
                                 .test_name = "NIC2 local address and interface",
                                 .send_local_addr_str = kClientIpv6Addr2,
                                 .send_local_if_str = kClientNic2Name,
                                 .expected_recv_addr_str = kClientIpv6Addr2,
                             }),
                         [](const testing::TestParamInfo<SendIpv6PacketInfoSuccessTestCase>& info) {
                           std::string test_name(info.param.test_name);
                           std::replace(test_name.begin(), test_name.end(), ' ', '_');
                           return test_name;
                         });

struct SendIpv6PacketInfoFailureTestCase {
  std::string test_name;
  std::string bind_addr_str;
  std::optional<std::string> bind_to_device;
  std::optional<std::string> send_local_addr_str;
  std::optional<std::string> send_local_if_str;
  ssize_t expected_errno;
};

class SendIpv6PacketInfoFailureTest
    : public testing::TestWithParam<SendIpv6PacketInfoFailureTestCase> {};

TEST_P(SendIpv6PacketInfoFailureTest, CheckError) {
  fbl::unique_fd s;
  ASSERT_TRUE(s = fbl::unique_fd(socket(AF_INET6, SOCK_DGRAM, 0))) << strerror(errno);

  const auto [test_name, bind_addr_str, bind_to_device, send_local_addr_str, send_local_if_str,
              expected_errno] = GetParam();

  if (bind_to_device.has_value()) {
    ASSERT_EQ(setsockopt(s.get(), SOL_SOCKET, SO_BINDTODEVICE, bind_to_device->c_str(),
                         static_cast<socklen_t>(bind_to_device->size())),
              0)
        << strerror(errno);
  }

  sockaddr_in6 bind_addr{
      .sin6_family = AF_INET6,
  };
  ASSERT_EQ(inet_pton(bind_addr.sin6_family, bind_addr_str.c_str(), &bind_addr.sin6_addr), 1);
  ASSERT_EQ(bind(s.get(), reinterpret_cast<const sockaddr*>(&bind_addr), sizeof(bind_addr)), 0)
      << strerror(errno);

  sockaddr_in6 to_addr = {
      .sin6_family = AF_INET6,
      .sin6_port = htons(kServerPort),
  };
  ASSERT_EQ(inet_pton(to_addr.sin6_family, kServerIpv6Addr, &to_addr.sin6_addr), 1);

  in6_pktinfo send_pktinfo = {};
  if (send_local_addr_str.has_value()) {
    ASSERT_EQ(inet_pton(AF_INET6, send_local_addr_str->c_str(), &send_pktinfo.ipi6_addr), 1);
  }
  if (send_local_if_str.has_value()) {
    send_pktinfo.ipi6_ifindex = if_nametoindex(send_local_if_str->c_str());
  }

  char send_buf[] = "hello";
  iovec send_iovec = {
      .iov_base = send_buf,
      .iov_len = sizeof(send_buf),
  };
  char send_control[CMSG_SPACE(sizeof(send_pktinfo)) + 1];
  msghdr send_msghdr = {
      .msg_name = &to_addr,
      .msg_namelen = sizeof(to_addr),
      .msg_iov = &send_iovec,
      .msg_iovlen = 1,
      .msg_control = send_control,
      .msg_controllen = sizeof(send_control),
  };
  cmsghdr* cmsg = CMSG_FIRSTHDR(&send_msghdr);
  ASSERT_NE(cmsg, nullptr);
  cmsg->cmsg_len = CMSG_LEN(sizeof(send_pktinfo));
  cmsg->cmsg_level = SOL_IPV6;
  cmsg->cmsg_type = IPV6_PKTINFO;
  memcpy(CMSG_DATA(cmsg), &send_pktinfo, sizeof(send_pktinfo));
  ASSERT_EQ(sendmsg(s.get(), &send_msghdr, 0), -1);
  EXPECT_EQ(errno, expected_errno) << strerror(errno);
}

constexpr char kIpv6UnspecifiedAddr[] = "::";

INSTANTIATE_TEST_SUITE_P(SocketTests, SendIpv6PacketInfoFailureTest,
                         testing::Values(
                             SendIpv6PacketInfoFailureTestCase{
                                 .test_name = "Local interface and bound device mismatch",
                                 .bind_addr_str = kIpv6UnspecifiedAddr,
                                 .bind_to_device = kClientNic2Name,
                                 .send_local_if_str = kClientNic1Name,
                                 .expected_errno = EHOSTUNREACH,
                             },
                             SendIpv6PacketInfoFailureTestCase{
                                 .test_name = "Local address and bound device mismatch",
                                 .bind_addr_str = kIpv6UnspecifiedAddr,
                                 .bind_to_device = kClientNic2Name,
                                 .send_local_addr_str = kClientIpv6Addr1,
                                 .expected_errno = EADDRNOTAVAIL,
                             },
                             SendIpv6PacketInfoFailureTestCase{
                                 .test_name = "Local addr and interface mismatch",
                                 .bind_addr_str = kIpv6UnspecifiedAddr,
                                 .send_local_addr_str = kClientIpv6Addr2,
                                 .send_local_if_str = kClientNic1Name,
                                 .expected_errno = EADDRNOTAVAIL,
                             },
                             SendIpv6PacketInfoFailureTestCase{
                                 .test_name = "Bound address and local interface mismatch",
                                 .bind_addr_str = kClientIpv6Addr1,
                                 .send_local_if_str = kClientNic2Name,
                                 .expected_errno = EADDRNOTAVAIL}),
                         [](const testing::TestParamInfo<SendIpv6PacketInfoFailureTestCase>& info) {
                           std::string test_name(info.param.test_name);
                           std::replace(test_name.begin(), test_name.end(), ' ', '_');
                           return test_name;
                         });

}  // namespace
