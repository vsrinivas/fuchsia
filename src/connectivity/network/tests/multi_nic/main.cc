// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <arpa/inet.h>
#include <fidl/fuchsia.netemul.sync/cpp/wire.h>
#include <lib/fdio/directory.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <zircon/assert.h>

#include <fbl/unique_fd.h>
#include <gtest/gtest.h>

#include "constants.h"

namespace {

constexpr char kBusName[] = "test-bus";
constexpr char kTestClientName[] = "client";
constexpr char kTestServerName[] = "server";

void TestUdpPing(int domain, const sockaddr* bind_addr, socklen_t bind_addr_len,
                 const sockaddr* connect_addr, socklen_t connect_addr_len) {
  fbl::unique_fd s;
  ASSERT_TRUE(s = fbl::unique_fd(socket(domain, SOCK_DGRAM, 0))) << strerror(errno);

  ASSERT_EQ(bind(s.get(), bind_addr, bind_addr_len), 0) << strerror(errno);

  ASSERT_EQ(connect(s.get(), connect_addr, connect_addr_len), 0) << strerror(errno);

  constexpr char kSendBuf[] = "Hello";
  ASSERT_EQ(send(s.get(), &kSendBuf, sizeof(kSendBuf), 0), static_cast<ssize_t>(sizeof(kSendBuf)))
      << strerror(errno);

  constexpr char kExpectedRecvBuf[] = "Response: Hello";
  char recv_buf[sizeof(kExpectedRecvBuf) + 1];
  ASSERT_EQ(read(s.get(), &recv_buf, sizeof(recv_buf)),
            static_cast<ssize_t>(sizeof(kExpectedRecvBuf)))
      << strerror(errno);
  EXPECT_STREQ(kExpectedRecvBuf, recv_buf);
}

void TestIpv4UdpPing(const char* client_addr) {
  sockaddr_in bind_addr{
      .sin_family = AF_INET,
  };
  ASSERT_EQ(inet_pton(bind_addr.sin_family, client_addr, &bind_addr.sin_addr), 1);

  sockaddr_in connect_addr = {
      .sin_family = AF_INET,
      .sin_port = htons(kServerPort),
  };
  ASSERT_EQ(inet_pton(connect_addr.sin_family, kServerIpv4Addr, &connect_addr.sin_addr), 1);

  ASSERT_NO_FATAL_FAILURE(TestUdpPing(AF_INET, reinterpret_cast<sockaddr*>(&bind_addr),
                                      sizeof(bind_addr), reinterpret_cast<sockaddr*>(&connect_addr),
                                      sizeof(connect_addr)));
}

TEST(InfraTest, UdpPingFromIpv4Ep1) { ASSERT_NO_FATAL_FAILURE(TestIpv4UdpPing(kClientIpv4Addr1)); }

TEST(InfraTest, UdpPingFromIpv4Ep2) { ASSERT_NO_FATAL_FAILURE(TestIpv4UdpPing(kClientIpv4Addr2)); }

void TestIpv6UdpPing(const char* client_addr) {
  sockaddr_in6 bind_addr{
      .sin6_family = AF_INET6,
  };
  ASSERT_EQ(inet_pton(bind_addr.sin6_family, client_addr, &bind_addr.sin6_addr), 1);

  sockaddr_in6 connect_addr = {
      .sin6_family = AF_INET6,
      .sin6_port = htons(kServerPort),
  };
  ASSERT_EQ(inet_pton(connect_addr.sin6_family, kServerIpv6Addr, &connect_addr.sin6_addr), 1);

  ASSERT_NO_FATAL_FAILURE(TestUdpPing(AF_INET6, reinterpret_cast<sockaddr*>(&bind_addr),
                                      sizeof(bind_addr), reinterpret_cast<sockaddr*>(&connect_addr),
                                      sizeof(connect_addr)));
}

TEST(InfraTest, UdpPingFromIpv6Ep1) { ASSERT_NO_FATAL_FAILURE(TestIpv6UdpPing(kClientIpv6Addr1)); }

TEST(InfraTest, UdpPingFromIpv6Ep2) { ASSERT_NO_FATAL_FAILURE(TestIpv6UdpPing(kClientIpv6Addr2)); }

}  // namespace

int main(int argc, char** argv) {
  // Make sure the server has come up before running any tests by waiting
  // on the bus for its subscription.
  auto sync_manager_endpoints = fidl::CreateEndpoints<fuchsia_netemul_sync::SyncManager>();
  if (sync_manager_endpoints.is_error()) {
    ZX_PANIC("error creating sync manager endpoints: %s",
             zx_status_get_string(sync_manager_endpoints.error_value()));
  }
  auto bus_endpoints = fidl::CreateEndpoints<fuchsia_netemul_sync::Bus>();
  if (bus_endpoints.is_error()) {
    ZX_PANIC("error creating bus endpoints: %s", zx_status_get_string(bus_endpoints.error_value()));
  }

  zx_status_t status = fdio_service_connect_by_name(
      fidl::DiscoverableProtocolName<fuchsia_netemul_sync::SyncManager>,
      sync_manager_endpoints->server.channel().release());
  ZX_ASSERT_MSG(status == ZX_OK, "error connecting to sync manager server: %s",
                zx_status_get_string(status));

  fidl::WireSyncClient sync_manager =
      fidl::WireSyncClient(std::move(sync_manager_endpoints->client));
  {
    fidl::Status result =
        sync_manager->BusSubscribe(kBusName, kTestClientName, std::move(bus_endpoints->server));
    ZX_ASSERT_MSG(result.status() == ZX_OK, "error getting bus: %s", result.status_string());
  }

  fidl::WireSyncClient bus{std::move(bus_endpoints->client)};
  {
    std::array<fidl::StringView, 1> clients = {fidl::StringView(kTestServerName)};
    fidl::Status result = bus->WaitForClients(
        fidl::VectorView<fidl::StringView>::FromExternal(clients), /* no timeout */ 0);
    ZX_ASSERT_MSG(result.status() == ZX_OK, "error waiting for server to be ready: %s",
                  result.status_string());
  }

  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
