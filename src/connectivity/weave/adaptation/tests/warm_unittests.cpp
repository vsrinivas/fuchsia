// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/net/cpp/fidl.h>
#include <fuchsia/netstack/cpp/fidl_test_base.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/syslog/cpp/macros.h>

#include <algorithm>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

// clang-format off
#include <Weave/DeviceLayer/internal/WeaveDeviceLayerInternal.h>
#include <Weave/DeviceLayer/ConnectivityManager.h>
#include <Weave/DeviceLayer/ThreadStackManager.h>
#include <Warm/Warm.h>
// clang-format on

#include "src/connectivity/weave/adaptation/thread_stack_manager_delegate_impl.h"
#include "weave_test_fixture.h"

namespace nl {
namespace Weave {
namespace Warm {
namespace Platform {
namespace testing {

namespace {
using fuchsia::netstack::NetInterface2;
using fuchsia::netstack::RouteTableEntry2;
using fuchsia::netstack::Status;

using DeviceLayer::PlatformMgrImpl;
using DeviceLayer::ThreadStackMgrImpl;
using DeviceLayer::Internal::testing::WeaveTestFixture;

constexpr char kTunInterfaceName[] = "weav-tun0";
constexpr char kThreadInterfaceName[] = "lowpan0";

constexpr uint32_t kRouteMetric_HighPriority = 0;
constexpr uint32_t kRouteMetric_MediumPriority = 99;
constexpr uint32_t kRouteMetric_LowPriority = 999;

// Comparison function to check if two instances of fuchsia::net::IpAddress
// match their address space.
bool CompareIpAddress(const ::fuchsia::net::IpAddress& right,
                      const ::fuchsia::net::IpAddress& left) {
  return std::memcmp(right.ipv6().addr.data(), left.ipv6().addr.data(), right.ipv6().addr.size()) ==
         0;
}

// Comparison function to compare Weave's Inet::IPAddress with Fuchsia's
// fuchsia::net::IpAddress match in their address space..
bool CompareIpAddress(const ::nl::Inet::IPAddress& right, const ::fuchsia::net::IpAddress& left) {
  fuchsia::net::Ipv6Address v6;
  std::memcpy(v6.addr.data(), right.Addr, v6.addr.size());
  fuchsia::net::IpAddress right_v6;
  right_v6.set_ipv6(v6);
  return CompareIpAddress(right_v6, left);
}

}  // namespace

// Fake implementation of TSM delegate, only provides an interface name.
class FakeThreadStackManagerDelegate : public DeviceLayer::ThreadStackManagerDelegateImpl {
 public:
  WEAVE_ERROR InitThreadStack() override { return WEAVE_NO_ERROR; }
  std::string GetInterfaceName() const override { return kThreadInterfaceName; }
};

// Fake implementation of fuchsia::netstack::Netstack that provides the minimal
// functionality required for WARM to run.
class FakeNetstack : public fuchsia::netstack::testing::Netstack_TestBase,
                     public fuchsia::netstack::testing::RouteTableTransaction_TestBase {
 private:
  void NotImplemented_(const std::string& name) override { FAIL() << "Not implemented: " << name; }

  // FIDL interface definitions.
  void GetInterfaces2(GetInterfaces2Callback callback) override {
    std::vector<NetInterface2> result(interfaces_.size());
    for (size_t i = 0; i < result.size(); ++i) {
      ASSERT_EQ(interfaces_[i].Clone(&result[i]), ZX_OK);
    }
    callback(std::move(result));
  }

  void SetInterfaceAddress(uint32_t nicid, ::fuchsia::net::IpAddress addr, uint8_t prefix_len,
                           SetInterfaceAddressCallback callback) override {
    // Confirm that the configured address is a V6 address.
    ASSERT_TRUE(addr.is_ipv6());

    // Find the interface with the specified ID and append the address.
    auto it = std::find_if(interfaces_.begin(), interfaces_.end(),
                           [&](const NetInterface2& interface) { return nicid == interface.id; });
    if (it == interfaces_.end()) {
      callback({.status = Status::UNKNOWN_INTERFACE});
      return;
    }

    auto& ipv6 = it->ipv6addrs.emplace_back();
    ipv6.prefix_len = prefix_len;
    ipv6.addr.set_ipv6(addr.ipv6());
    callback({.status = Status::OK});
  }

  void RemoveInterfaceAddress(uint32_t nicid, ::fuchsia::net::IpAddress addr, uint8_t prefix_len,
                              RemoveInterfaceAddressCallback callback) override {
    // Find the interface with the specified ID and remove the address.
    auto it = std::find_if(interfaces_.begin(), interfaces_.end(),
                           [&](const NetInterface2& interface) { return nicid == interface.id; });
    if (it == interfaces_.end()) {
      callback({.status = Status::UNKNOWN_INTERFACE});
      return;
    }

    auto addr_it = std::remove_if(
        it->ipv6addrs.begin(), it->ipv6addrs.end(),
        [&](const fuchsia::net::Subnet& ipv6) { return CompareIpAddress(addr, ipv6.addr); });
    if (addr_it == it->ipv6addrs.end()) {
      callback({.status = Status::UNKNOWN_ERROR});
      return;
    }
    it->ipv6addrs.erase(addr_it, it->ipv6addrs.end());
    callback({.status = Status::OK});
  }

  void StartRouteTableTransaction(
      ::fidl::InterfaceRequest<::fuchsia::netstack::RouteTableTransaction> route_table_transaction,
      StartRouteTableTransactionCallback callback) override {
    route_table_binding_.Bind(std::move(route_table_transaction), dispatcher_);
    callback(ZX_OK);
  }

  void AddRoute(::fuchsia::netstack::RouteTableEntry2 route_table_entry,
                AddRouteCallback callback) override {
    route_table_.push_back(std::move(route_table_entry));
    callback(ZX_OK);
  }

  void DelRoute(::fuchsia::netstack::RouteTableEntry2 route_table_entry,
                DelRouteCallback callback) override {
    auto it = std::remove_if(route_table_.begin(), route_table_.end(),
                             [&](const ::fuchsia::netstack::RouteTableEntry2& entry) {
                               return route_table_entry.nicid == entry.nicid &&
                                      route_table_entry.metric == entry.metric &&
                                      CompareIpAddress(route_table_entry.destination,
                                                       entry.destination) &&
                                      CompareIpAddress(route_table_entry.netmask, entry.netmask);
                             });
    if (it == route_table_.end()) {
      callback(ZX_ERR_NOT_FOUND);
      return;
    }
    route_table_.erase(it, route_table_.end());
    callback(ZX_OK);
  }

 public:
  // Mutators, accessors, and helpers for tests.

  // Add a fake interface with the given name. Does not check for duplicates.
  FakeNetstack& AddFakeInterface(std::string name) {
    fuchsia::net::Ipv4Address empty_v4;
    std::memset(empty_v4.addr.data(), 0xFF, empty_v4.addr.size());

    auto& interface = interfaces_.emplace_back();
    interface.id = ++last_id_assigned;
    interface.name = name;
    interface.addr.set_ipv4(empty_v4);
    interface.netmask.set_ipv4(empty_v4);
    interface.broadaddr.set_ipv4(empty_v4);
    return *this;
  }

  // Remove the fake interface with the given name. If it is not present, no change occurs.
  FakeNetstack& RemoveFakeInterface(std::string name) {
    auto it =
        std::remove_if(interfaces_.begin(), interfaces_.end(),
                       [&](const NetInterface2& interface) { return interface.name == name; });
    interfaces_.erase(it, interfaces_.end());
    return *this;
  }

  // Access the current interfaces.
  const std::vector<NetInterface2>& interfaces() const { return interfaces_; }

  // Access the current route table.
  const std::vector<RouteTableEntry2>& route_table() const { return route_table_; }

  // Get a pointer to an interface by name.
  void GetInterfaceByName(const std::string name, NetInterface2& interface) {
    auto it = std::find_if(interfaces_.begin(), interfaces_.end(),
                           [&](const NetInterface2& interface) { return interface.name == name; });
    ASSERT_NE(it, interfaces_.end());
    ASSERT_EQ(it->Clone(&interface), ZX_OK);
  }

  // Check if the given interface ID and address exists in the route table.
  bool FindRouteTableEntry(uint32_t nicid, ::nl::Inet::IPAddress addr,
                           uint32_t metric = kRouteMetric_HighPriority) {
    auto it = std::find_if(
        route_table_.begin(), route_table_.end(), [&](const RouteTableEntry2& route_table_entry) {
          return nicid == route_table_entry.nicid && metric == route_table_entry.metric &&
                 CompareIpAddress(addr, route_table_entry.destination);
        });

    return it != route_table_.end();
  }

  fidl::InterfaceRequestHandler<fuchsia::netstack::Netstack> GetHandler(
      async_dispatcher_t* dispatcher) {
    dispatcher_ = dispatcher;
    return [this](fidl::InterfaceRequest<fuchsia::netstack::Netstack> request) {
      binding_.Bind(std::move(request), dispatcher_);
    };
  }

 private:
  fidl::Binding<fuchsia::netstack::Netstack> binding_{this};
  fidl::Binding<fuchsia::netstack::RouteTableTransaction> route_table_binding_{this};
  async_dispatcher_t* dispatcher_;
  std::vector<NetInterface2> interfaces_;
  std::vector<RouteTableEntry2> route_table_;
  uint32_t last_id_assigned = 0;
};

class WarmTest : public testing::WeaveTestFixture<> {
 public:
  void SetUp() override {
    WeaveTestFixture<>::SetUp();

    // Initialize everything needed for the test.
    context_provider_.service_directory_provider()->AddService(
        fake_net_stack_.GetHandler(dispatcher()));
    PlatformMgrImpl().SetComponentContextForProcess(context_provider_.TakeContext());
    ThreadStackMgrImpl().SetDelegate(std::make_unique<FakeThreadStackManagerDelegate>());
    ThreadStackMgrImpl().InitThreadStack();
    Warm::Platform::Init(nullptr);

    // Populate initial fake interfaces
    fake_net_stack().AddFakeInterface(kTunInterfaceName);
    fake_net_stack().AddFakeInterface(ThreadStackMgrImpl().GetInterfaceName());

    RunFixtureLoop();
  }

  void TearDown() override {
    StopFixtureLoop();
    ThreadStackMgrImpl().SetDelegate(nullptr);
    WeaveTestFixture<>::TearDown();
  }

 protected:
  FakeNetstack& fake_net_stack() { return fake_net_stack_; }

  NetInterface2& GetThreadInterface(NetInterface2& interface) {
    fake_net_stack().GetInterfaceByName(ThreadStackMgrImpl().GetInterfaceName(), interface);
    return interface;
  }

  uint32_t GetThreadInterfaceId() {
    NetInterface2 interface;
    return GetThreadInterface(interface).id;
  }

  NetInterface2& GetTunnelInterface(NetInterface2& interface) {
    fake_net_stack().GetInterfaceByName(kTunInterfaceName, interface);
    return interface;
  }

  uint32_t GetTunnelInterfaceId() {
    NetInterface2 interface;
    return GetTunnelInterface(interface).id;
  }

 private:
  FakeNetstack fake_net_stack_;
  sys::testing::ComponentContextProvider context_provider_;
};

TEST_F(WarmTest, AddRemoveAddressThread) {
  constexpr char kSubnetIp[] = "2001:0DB8:0042::";
  constexpr uint8_t kPrefixLength = 48;
  NetInterface2 lowpan;
  Inet::IPAddress addr;

  // Sanity check - no addresses assigned.
  GetThreadInterface(lowpan);
  EXPECT_EQ(lowpan.ipv6addrs.size(), 0u);

  // Attempt to add the address.
  ASSERT_TRUE(Inet::IPAddress::FromString(kSubnetIp, addr));
  auto result = AddRemoveHostAddress(kInterfaceTypeThread, addr, kPrefixLength, /*add*/ true);
  EXPECT_EQ(result, kPlatformResultSuccess);

  // Confirm that it worked.
  GetThreadInterface(lowpan);
  ASSERT_EQ(lowpan.ipv6addrs.size(), 1u);
  EXPECT_TRUE(CompareIpAddress(addr, lowpan.ipv6addrs[0].addr));

  // Attempt to remove the address.
  result = AddRemoveHostAddress(kInterfaceTypeThread, addr, kPrefixLength, /*add*/ false);
  EXPECT_EQ(result, kPlatformResultSuccess);

  // Confirm that it worked.
  GetThreadInterface(lowpan);
  EXPECT_EQ(lowpan.ipv6addrs.size(), 0u);
}

TEST_F(WarmTest, AddRemoveAddressTunnel) {
  constexpr char kSubnetIp[] = "2001:0DB8:0042::";
  constexpr uint8_t kPrefixLength = 48;
  NetInterface2 weave_tun;
  Inet::IPAddress addr;

  // Sanity check - no addresses assigned.
  GetTunnelInterface(weave_tun);
  EXPECT_EQ(weave_tun.ipv6addrs.size(), 0u);

  // Attempt to add the address.
  ASSERT_TRUE(Inet::IPAddress::FromString(kSubnetIp, addr));
  auto result = AddRemoveHostAddress(kInterfaceTypeTunnel, addr, kPrefixLength, /*add*/ true);
  EXPECT_EQ(result, kPlatformResultSuccess);

  // Confirm that it worked.
  GetTunnelInterface(weave_tun);
  ASSERT_EQ(weave_tun.ipv6addrs.size(), 1u);
  EXPECT_TRUE(CompareIpAddress(addr, weave_tun.ipv6addrs[0].addr));

  // Attempt to remove the address.
  result = AddRemoveHostAddress(kInterfaceTypeTunnel, addr, kPrefixLength, /*add*/ false);
  EXPECT_EQ(result, kPlatformResultSuccess);

  // Confirm that it worked.
  GetTunnelInterface(weave_tun);
  EXPECT_EQ(weave_tun.ipv6addrs.size(), 0u);
}

TEST_F(WarmTest, RemoveAddressThreadNotFound) {
  constexpr char kSubnetIp[] = "2001:0DB8:0042::";
  constexpr uint8_t kPrefixLength = 48;
  NetInterface2 lowpan;
  Inet::IPAddress addr;

  // Sanity check - no addresses assigned.
  GetThreadInterface(lowpan);
  EXPECT_EQ(lowpan.ipv6addrs.size(), 0u);

  // Attempt to remove the address, expecting failure.
  ASSERT_TRUE(Inet::IPAddress::FromString(kSubnetIp, addr));
  auto result = AddRemoveHostAddress(kInterfaceTypeThread, addr, kPrefixLength, /*add*/ false);
  EXPECT_EQ(result, kPlatformResultFailure);

  // Sanity check - still no addresses assigned.
  GetThreadInterface(lowpan);
  EXPECT_EQ(lowpan.ipv6addrs.size(), 0u);
}

TEST_F(WarmTest, RemoveAddressTunnelNotFound) {
  constexpr char kSubnetIp[] = "2001:0DB8:0042::";
  constexpr uint8_t kPrefixLength = 48;
  NetInterface2 weave_tun;
  Inet::IPAddress addr;

  // Sanity check - no addresses assigned.
  GetTunnelInterface(weave_tun);
  EXPECT_EQ(weave_tun.ipv6addrs.size(), 0u);

  // Attempt to remove the address, expecting failure.
  ASSERT_TRUE(Inet::IPAddress::FromString(kSubnetIp, addr));
  auto result = AddRemoveHostAddress(kInterfaceTypeTunnel, addr, kPrefixLength, /*add*/ false);
  EXPECT_EQ(result, kPlatformResultFailure);

  // Sanity check - still no addresses assigned.
  GetTunnelInterface(weave_tun);
  EXPECT_EQ(weave_tun.ipv6addrs.size(), 0u);
}

TEST_F(WarmTest, AddAddressThreadNoInterface) {
  constexpr char kSubnetIp[] = "2001:0DB8:0042::";
  constexpr uint8_t kPrefixLength = 48;
  Inet::IPAddress addr;

  fake_net_stack().RemoveFakeInterface(kThreadInterfaceName);

  // Attempt to add to the interface when there's no Thread interface. Expect failure.
  ASSERT_TRUE(Inet::IPAddress::FromString(kSubnetIp, addr));
  auto result = AddRemoveHostAddress(kInterfaceTypeThread, addr, kPrefixLength, /*add*/ true);
  EXPECT_EQ(result, kPlatformResultFailure);
}

TEST_F(WarmTest, RemoveAddressThreadNoInterface) {
  constexpr char kSubnetIp[] = "2001:0DB8:0042::";
  constexpr uint8_t kPrefixLength = 48;
  Inet::IPAddress addr;

  fake_net_stack().RemoveFakeInterface(kThreadInterfaceName);

  // Attempt to remove from the interface when there's no Thread interface. Expect success.
  ASSERT_TRUE(Inet::IPAddress::FromString(kSubnetIp, addr));
  auto result = AddRemoveHostAddress(kInterfaceTypeThread, addr, kPrefixLength, /*add*/ false);
  EXPECT_EQ(result, kPlatformResultSuccess);
}

TEST_F(WarmTest, AddAddressTunnelNoInterface) {
  constexpr char kSubnetIp[] = "2001:0DB8:0042::";
  constexpr uint8_t kPrefixLength = 48;
  Inet::IPAddress addr;

  fake_net_stack().RemoveFakeInterface(kTunInterfaceName);

  // Attempt to add to the interface when there's no Tunnel interface. Expect failure.
  ASSERT_TRUE(Inet::IPAddress::FromString(kSubnetIp, addr));
  auto result = AddRemoveHostAddress(kInterfaceTypeTunnel, addr, kPrefixLength, /*add*/ true);
  EXPECT_EQ(result, kPlatformResultFailure);
}

TEST_F(WarmTest, RemoveAddressTunnelNoInterface) {
  constexpr char kSubnetIp[] = "2001:0DB8:0042::";
  constexpr uint8_t kPrefixLength = 48;
  Inet::IPAddress addr;

  fake_net_stack().RemoveFakeInterface(kTunInterfaceName);

  // Attempt to remove from the interface when there's no Tunnel interface. Expect success.
  ASSERT_TRUE(Inet::IPAddress::FromString(kSubnetIp, addr));
  auto result = AddRemoveHostAddress(kInterfaceTypeTunnel, addr, kPrefixLength, /*add*/ false);
  EXPECT_EQ(result, kPlatformResultSuccess);
}

TEST_F(WarmTest, AddRemoveHostRouteThread) {
  constexpr char kSubnetIp[] = "2001:0DB8:0042::";
  constexpr uint8_t kPrefixLength = 48;
  Inet::IPPrefix prefix;

  ASSERT_TRUE(Inet::IPAddress::FromString(kSubnetIp, prefix.IPAddr));
  prefix.Length = kPrefixLength;

  // Sanity check - confirm no routes to the Thread interface exist.
  uint64_t thread_iface_id = GetThreadInterfaceId();
  ASSERT_NE(thread_iface_id, 0u);
  EXPECT_FALSE(fake_net_stack().FindRouteTableEntry(thread_iface_id, prefix.IPAddr));

  // Attempt to add a route to the Thread interface.
  auto result = AddRemoveHostRoute(kInterfaceTypeThread, prefix, kRoutePriorityHigh, /*add*/ true);
  EXPECT_EQ(result, kPlatformResultSuccess);

  // Confirm that a route exists to the Thread interface with the given IP.
  EXPECT_TRUE(fake_net_stack().FindRouteTableEntry(thread_iface_id, prefix.IPAddr));

  // Remove the route to the Thread interface.
  result = AddRemoveHostRoute(kInterfaceTypeThread, prefix, kRoutePriorityHigh, /*add*/ false);
  EXPECT_EQ(result, kPlatformResultSuccess);

  // Confirm that the removal worked.
  EXPECT_FALSE(fake_net_stack().FindRouteTableEntry(thread_iface_id, prefix.IPAddr));
}

TEST_F(WarmTest, AddRemoveHostRouteTunnel) {
  constexpr char kSubnetIp[] = "2001:0DB8:0042::";
  constexpr uint8_t kPrefixLength = 48;
  Inet::IPPrefix prefix;

  ASSERT_TRUE(Inet::IPAddress::FromString(kSubnetIp, prefix.IPAddr));
  prefix.Length = kPrefixLength;

  // Sanity check - confirm no routes to the Tunnel interface exist.
  uint64_t tunnel_iface_id = GetTunnelInterfaceId();
  ASSERT_NE(tunnel_iface_id, 0u);
  EXPECT_FALSE(fake_net_stack().FindRouteTableEntry(tunnel_iface_id, prefix.IPAddr));

  // Attempt to add a route to the Tunnel interface.
  auto result = AddRemoveHostRoute(kInterfaceTypeTunnel, prefix, kRoutePriorityHigh, /*add*/ true);
  EXPECT_EQ(result, kPlatformResultSuccess);

  // Confirm that a route exists to the Tunnel interface with the given IP.
  EXPECT_TRUE(fake_net_stack().FindRouteTableEntry(tunnel_iface_id, prefix.IPAddr));

  // Remove the route to the Tunnel interface.
  result = AddRemoveHostRoute(kInterfaceTypeTunnel, prefix, kRoutePriorityHigh, /*add*/ false);
  EXPECT_EQ(result, kPlatformResultSuccess);

  // Confirm that the removal worked.
  EXPECT_FALSE(fake_net_stack().FindRouteTableEntry(tunnel_iface_id, prefix.IPAddr));
}

TEST_F(WarmTest, RemoveHostRouteThreadNotFound) {
  constexpr char kSubnetIp[] = "2001:0DB8:0042::";
  constexpr uint8_t kPrefixLength = 48;
  Inet::IPPrefix prefix;

  ASSERT_TRUE(Inet::IPAddress::FromString(kSubnetIp, prefix.IPAddr));
  prefix.Length = kPrefixLength;

  // Sanity check - confirm no routes to the Thread interface exist.
  uint64_t thread_iface_id = GetThreadInterfaceId();
  ASSERT_NE(thread_iface_id, 0u);
  EXPECT_FALSE(fake_net_stack().FindRouteTableEntry(thread_iface_id, prefix.IPAddr));

  // Remove the non-existent route to the Thread interface, expect failure.
  auto result = AddRemoveHostRoute(kInterfaceTypeThread, prefix, kRoutePriorityHigh, /*add*/ false);
  EXPECT_EQ(result, kPlatformResultFailure);

  // Sanity check - confirm still no routes to the Thread interface exist.
  EXPECT_FALSE(fake_net_stack().FindRouteTableEntry(thread_iface_id, prefix.IPAddr));
}

TEST_F(WarmTest, RemoveHostRouteTunnelNotFound) {
  constexpr char kSubnetIp[] = "2001:0DB8:0042::";
  constexpr uint8_t kPrefixLength = 48;
  Inet::IPPrefix prefix;

  ASSERT_TRUE(Inet::IPAddress::FromString(kSubnetIp, prefix.IPAddr));
  prefix.Length = kPrefixLength;

  // Sanity check - confirm no routes to the Tunnel interface exist.
  uint64_t tunnel_iface_id = GetTunnelInterfaceId();
  ASSERT_NE(tunnel_iface_id, 0u);
  EXPECT_FALSE(fake_net_stack().FindRouteTableEntry(tunnel_iface_id, prefix.IPAddr));

  // Remove the non-existent route to the Tunnel interface, expect failure.
  auto result = AddRemoveHostRoute(kInterfaceTypeTunnel, prefix, kRoutePriorityHigh, /*add*/ false);
  EXPECT_EQ(result, kPlatformResultFailure);

  // Sanity check - confirm still no routes to the Tunnel interface exist.
  EXPECT_FALSE(fake_net_stack().FindRouteTableEntry(tunnel_iface_id, prefix.IPAddr));
}

TEST_F(WarmTest, AddHostRouteTunnelRoutePriorities) {
  constexpr char kSubnetIp[] = "2001:0DB8:0042::";
  constexpr uint8_t kPrefixLength = 48;
  Inet::IPPrefix prefix;

  ASSERT_TRUE(Inet::IPAddress::FromString(kSubnetIp, prefix.IPAddr));
  prefix.Length = kPrefixLength;

  // Sanity check - confirm no routes to the tunnel interface exist.
  uint64_t tunnel_iface_id = GetTunnelInterfaceId();
  ASSERT_NE(tunnel_iface_id, 0u);
  EXPECT_FALSE(fake_net_stack().FindRouteTableEntry(tunnel_iface_id, prefix.IPAddr));

  // Sanity check - confirm no routes to the lowpan interface exist.
  uint64_t thread_iface_id = GetThreadInterfaceId();
  ASSERT_NE(thread_iface_id, 0u);
  EXPECT_FALSE(fake_net_stack().FindRouteTableEntry(thread_iface_id, prefix.IPAddr));

  // Add a high-priority route to the tunnel interface.
  auto result = AddRemoveHostRoute(kInterfaceTypeTunnel, prefix, kRoutePriorityHigh, /*add*/ true);
  EXPECT_EQ(result, kPlatformResultSuccess);

  // Add a medium-priority route to the lowpan interface.
  result = AddRemoveHostRoute(kInterfaceTypeThread, prefix, kRoutePriorityMedium, /*add*/ true);
  EXPECT_EQ(result, kPlatformResultSuccess);

  // Add a low-priority route to the lowpan interface.
  result = AddRemoveHostRoute(kInterfaceTypeTunnel, prefix, kRoutePriorityLow, /*add*/ true);
  EXPECT_EQ(result, kPlatformResultSuccess);

  // Confirm all three priority routes exist.
  EXPECT_TRUE(fake_net_stack().FindRouteTableEntry(tunnel_iface_id, prefix.IPAddr,
                                                   kRouteMetric_HighPriority));
  EXPECT_TRUE(fake_net_stack().FindRouteTableEntry(thread_iface_id, prefix.IPAddr,
                                                   kRouteMetric_MediumPriority));
  EXPECT_TRUE(fake_net_stack().FindRouteTableEntry(tunnel_iface_id, prefix.IPAddr,
                                                   kRouteMetric_LowPriority));
}

}  // namespace testing
}  // namespace Platform
}  // namespace Warm
}  // namespace Weave
}  // namespace nl
