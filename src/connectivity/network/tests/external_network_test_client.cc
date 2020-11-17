// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// These tests run with an external network interface providing default route
// addresses.

#include <arpa/inet.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/utsname.h>

#include <fbl/unique_fd.h>
#include <gtest/gtest.h>

namespace {

// This is the expected derived device name for the mac address
// aa:bb:cc:dd:ee:ff, specified in meta/netstack_external_network_test.cmx
// (see facets.fuchsia.netemul.networks.endpoints[0].mac).
//
// Since this only used on Fuchsia, it is conditionally compiled.
#if defined(__Fuchsia__)
const char kDerivedDeviceName[] = "train-cache-uncle-chill";
#endif

TEST(ExternalNetworkTest, ConnectToNonRoutableINET) {
  int s;
  ASSERT_GE(s = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0), 0) << strerror(errno);

  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;

  // RFC5737#section-3
  //
  // The blocks 192.0.2.0/24 (TEST-NET-1), 198.51.100.0/24 (TEST-NET-2),and
  // 203.0.113.0/24 (TEST-NET-3) are provided for use in documentation.
  ASSERT_EQ(inet_pton(AF_INET, "192.0.2.55", &addr.sin_addr), 1) << strerror(errno);

  addr.sin_port = htons(1337);

  ASSERT_EQ(connect(s, reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr)), -1);

  // The host env (linux) may have a route to the remote (e.g. default route),
  // resulting in a TCP handshake being attempted and errno being set to
  // EINPROGRESS. In a fuchsia environment, errno will never be set to
  // EINPROGRESS because a TCP handshake will never be performed (the test is
  // run without network configurations that make the remote routable).
  //
  // TODO(fxbug.dev/46817): Set errno to the same value as linux when a remote is
  // unroutable.
#if defined(__linux__)
  ASSERT_TRUE(errno == EINPROGRESS || errno == ENETUNREACH) << strerror(errno);
#else
  ASSERT_EQ(errno, EHOSTUNREACH) << strerror(errno);
#endif

  ASSERT_EQ(close(s), 0) << strerror(errno);
}

TEST(ExternalNetworkTest, ConnectToNonRoutableINET6) {
  int s;
  ASSERT_GE(s = socket(AF_INET6, SOCK_STREAM | SOCK_NONBLOCK, 0), 0) << strerror(errno);

  struct sockaddr_in6 addr = {};
  addr.sin6_family = AF_INET6;

  // RFC3849#section-2
  //
  // The prefix allocated for documentation purposes is 2001:DB8::/32.
  ASSERT_EQ(inet_pton(AF_INET6, "2001:db8::55", &addr.sin6_addr), 1) << strerror(errno);

  addr.sin6_port = htons(1337);

  ASSERT_EQ(connect(s, reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr)), -1);

  // The host env (linux) may have a route to the remote (e.g. default route),
  // resulting in a TCP handshake being attempted and errno being set to
  // EINPROGRESS. In a fuchsia environment, errno will never be set to
  // EINPROGRESS because a TCP handshake will never be performed (the test is
  // run without network configurations that make the remote routable).
  //
  // TODO(fxbug.dev/46817): Set errno to the same value as linux when a remote is
  // unroutable.
#if defined(__linux__)
  ASSERT_TRUE(errno == EINPROGRESS || errno == ENETUNREACH) << strerror(errno);
#else
  ASSERT_EQ(errno, EHOSTUNREACH) << strerror(errno);
#endif

  ASSERT_EQ(close(s), 0) << strerror(errno);
}

TEST(ExternalNetworkTest, GetHostName) {
  char hostname[HOST_NAME_MAX];
  EXPECT_GE(gethostname(hostname, sizeof(hostname)), 0) << strerror(errno);
#if defined(__Fuchsia__)
  ASSERT_STREQ(hostname, kDerivedDeviceName);
#endif
}

TEST(ExternalNetworkTest, Uname) {
  utsname uts;
  EXPECT_EQ(uname(&uts), 0) << strerror(errno);
#if defined(__Fuchsia__)
  ASSERT_STREQ(uts.nodename, kDerivedDeviceName);
#endif
}

TEST(ExternalNetworkTest, ConnectToRoutableNonexistentINET) {
  int fd;
  ASSERT_GE(fd = socket(AF_INET, SOCK_STREAM, 0), 0) << strerror(errno);

  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  // Connect to a routable address to a non-existing remote. This triggers ARP resolution which is
  // expected to fail.
  addr.sin_addr.s_addr = htonl(0xd0e0a0d);
  addr.sin_port = htons(1337);

  EXPECT_EQ(connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)), -1);
  // TODO(tamird): match linux. https://github.com/google/gvisor/issues/923.
#if defined(__linux__)
  EXPECT_EQ(errno, ETIMEDOUT) << strerror(errno);
#else
  EXPECT_EQ(errno, EHOSTUNREACH) << strerror(errno);
#endif

  EXPECT_EQ(close(fd), 0) << strerror(errno);
}

// Test to ensure UDP send doesn`t error even with ARP timeouts.
// TODO(fxb.dev/35006): Test needs to be extended or replicated to test
// against other transport send errors.
TEST(ExternalNetworkTest, UDPErrSend) {
  int sendfd;
  ASSERT_GE(sendfd = socket(AF_INET, SOCK_DGRAM, 0), 0) << strerror(errno);

  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(1337);
  char bytes[64];
  // Assign to a ssize_t variable to avoid compiler warnings for signedness in the EXPECTs below.
  ssize_t len = sizeof(bytes);

  // Precondition sanity check: write completes without error.
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  EXPECT_EQ(sendto(sendfd, bytes, sizeof(bytes), 0, reinterpret_cast<struct sockaddr*>(&addr),
                   sizeof(addr)),
            len)
      << strerror(errno);

  // Send to a routable address to a non-existing remote. This triggers ARP resolution which is
  // expected to fail, but that failure is expected to leave the socket alive. Before the change
  // that added this test, the socket would be incorrectly shut down.
  addr.sin_addr.s_addr = htonl(0xd0e0a0d);
  ssize_t ret = sendto(sendfd, bytes, sizeof(bytes), 0, reinterpret_cast<struct sockaddr*>(&addr),
                       sizeof(addr));

  // TODO(tamird): Why does linux not signal an error? Should we do the same?
#if defined(__linux__)
  EXPECT_EQ(ret, len) << strerror(errno);
#else
  EXPECT_EQ(ret, -1);
  EXPECT_EQ(errno, EHOSTUNREACH) << strerror(errno);
#endif

  // Postcondition sanity check: write completes without error.
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  EXPECT_EQ(sendto(sendfd, bytes, sizeof(bytes), 0, reinterpret_cast<struct sockaddr*>(&addr),
                   sizeof(addr)),
            len)
      << strerror(errno);

  EXPECT_EQ(close(sendfd), 0) << strerror(errno);
}

TEST(ExternalNetworkTest, IoctlGetInterfaceAddresses) {
  fbl::unique_fd fd;
  ASSERT_TRUE(fd = fbl::unique_fd(socket(AF_INET, SOCK_DGRAM, 0))) << strerror(errno);

  // If `ifc_req` is NULL, SIOCGIFCONF should return the necessary buffer size
  // in bytes for receiving all available addresses in ifc_len. This allows the
  // caller to determine the necessary buffer size beforehand.
  // See: https://man7.org/linux/man-pages/man7/netdevice.7.html
  struct ifconf ifc {
    .ifc_req = nullptr,
  };
  ASSERT_EQ(ioctl(fd.get(), SIOCGIFCONF, &ifc), 0) << strerror(errno);

  struct ifaddr {
    const char* name;
    const char* addr;
  };
#if defined(__linux__)
  // On Linux, only verify the loopback interface, because we don't know which
  // interfaces will exist when this test is run on host (as opposed to when it
  // runs in an emulated network environment on Fuchsia).
  const struct ifaddr expected[] = {
      {
          .name = "lo",
          .addr = "127.0.0.1",
      },
  };
  ASSERT_GE(ifc.ifc_len, static_cast<int>(std::size(expected) * sizeof(struct ifreq)));
#else
  const struct ifaddr expected[] = {
      // The loopback interface should always be present on Fuchsia.
      {
          .name = "lo",
          .addr = "127.0.0.1",
      },
      // This interface with two static addresses is configured in the emulated
      // network environment in which this test is run. This configuration is in
      // this component manifest: meta/netstack_external_network_test.cmx.
      {
          .name = "device",
          .addr = "192.168.0.1",
      },
      {
          .name = "device",
          .addr = "192.168.0.2",
      },
  };
  ASSERT_EQ(ifc.ifc_len, static_cast<int>(std::size(expected) * sizeof(struct ifreq)));
#endif
  ASSERT_EQ(ifc.ifc_req, nullptr);

  // Get the interface configuration information.
  // Pass a buffer that is double the required size and verify that nothing is
  // written past `ifc.ifc_len`.
  struct ifreq ifreq_buffer[ifc.ifc_len / sizeof(struct ifreq) + 100];
  const char FILLER = 0xa;
  memset(ifreq_buffer, FILLER, sizeof(ifreq_buffer));
  ifc.ifc_req = ifreq_buffer;
  ASSERT_EQ(ioctl(fd.get(), SIOCGIFCONF, &ifc), 0) << strerror(errno);

  struct ifreq* ifr = ifc.ifc_req;
  for (const auto& expected_ifaddr : expected) {
    ASSERT_STREQ(ifr->ifr_name, expected_ifaddr.name);

    ASSERT_EQ(ifr->ifr_addr.sa_family, AF_INET);
    auto addr = reinterpret_cast<const struct sockaddr_in*>(&ifr->ifr_addr);
    ASSERT_EQ(addr->sin_port, 0);
    char addrbuf[INET_ADDRSTRLEN];
    const char* addrstr = inet_ntop(AF_INET, &addr->sin_addr, addrbuf, sizeof(addrbuf));
    ASSERT_STREQ(addrstr, expected_ifaddr.addr);

    ifr++;
  }
  // Verify that the `ifc.ifc_req` buffer has not been overwritten past
  // `ifc.ifc_len`.
  char* buffer = reinterpret_cast<char*>(ifc.ifc_req);
  for (size_t i = ifc.ifc_len; i < sizeof(ifreq_buffer); i++) {
    EXPECT_EQ(buffer[i], FILLER) << i;
  }
}

}  // namespace
