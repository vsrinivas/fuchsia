// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/lowpan/device/cpp/fidl_test_base.h>
#include <fuchsia/net/cpp/fidl.h>
#include <fuchsia/net/interfaces/cpp/fidl_test_base.h>
#include <fuchsia/net/stack/cpp/fidl_test_base.h>
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

#include "test_connectivity_manager.h"
#include "test_thread_stack_manager.h"
#include "weave_test_fixture.h"

namespace nl {
namespace Weave {
namespace Warm {
namespace Platform {
namespace testing {

namespace {
using weave::adaptation::testing::TestConnectivityManager;
using weave::adaptation::testing::TestThreadStackManager;

using fuchsia::lowpan::device::DeviceRoute;
using fuchsia::lowpan::device::Lookup_LookupDevice_Response;
using fuchsia::lowpan::device::Lookup_LookupDevice_Result;
using fuchsia::lowpan::device::OnMeshPrefix;
using fuchsia::lowpan::device::Protocols;
using fuchsia::lowpan::device::ServiceError;

using fuchsia::netstack::RouteTableEntry;
using fuchsia::netstack::Status;

using DeviceLayer::ConnectivityMgrImpl;
using DeviceLayer::PlatformMgrImpl;
using DeviceLayer::ThreadStackMgrImpl;
using DeviceLayer::Internal::testing::WeaveTestFixture;

constexpr char kTunInterfaceName[] = "weav-tun0";

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

struct OwnedInterface {
  uint64_t id;
  std::string name;
  fuchsia::net::IpAddress addr;
  std::vector<fuchsia::net::Subnet> ipv6addrs;
};

class FakeNetInterfaces : public fuchsia::net::interfaces::testing::State_TestBase,
                          public fuchsia::net::interfaces::testing::Watcher_TestBase {
 public:
  void InitializeInterfaces(const std::vector<OwnedInterface>& interfaces) {
    existing_events_.clear();
    for (const auto& interface : interfaces) {
      AddExistingInterface(interface);
    }

    fuchsia::net::interfaces::Empty idle_event;
    fuchsia::net::interfaces::Event event =
        fuchsia::net::interfaces::Event::WithIdle(std::move(idle_event));
    existing_events_.push_back(std::move(event));
  }

  void NotImplemented_(const std::string& name) override { FAIL() << "Not implemented: " << name; }

  fidl::InterfaceRequestHandler<fuchsia::net::interfaces::State> GetHandler(
      async_dispatcher_t* dispatcher) {
    dispatcher_ = dispatcher;
    return [this](fidl::InterfaceRequest<fuchsia::net::interfaces::State> request) {
      state_binding_.Bind(std::move(request), dispatcher_);
    };
  }

  void GetWatcher(fuchsia::net::interfaces::WatcherOptions options,
                  fidl::InterfaceRequest<fuchsia::net::interfaces::Watcher> watcher) override {
    events_.clear();
    for (auto& existing_event : existing_events_) {
      fuchsia::net::interfaces::Event event;
      existing_event.Clone(&event);
      events_.push_back(std::move(event));
    }
    watcher_binding_.Bind(std::move(watcher), dispatcher_);
  }

  void Watch(fuchsia::net::interfaces::Watcher::WatchCallback callback) override {
    watch_callback_ = std::move(callback);
    SendPendingEvent();
  }

  void SendPendingEvent() {
    if (events_.empty() || !watch_callback_) {
      return;
    }
    fuchsia::net::interfaces::Event event(std::move(events_.front()));
    events_.pop_front();
    watch_callback_(std::move(event));
    watch_callback_ = nullptr;
  }

  void Close(zx_status_t epitaph_value = ZX_OK) {
    watcher_binding_.Close(epitaph_value);
    state_binding_.Close(epitaph_value);
  }

 private:
  void AddExistingInterface(const OwnedInterface& interface) {
    fuchsia::net::interfaces::Event event;
    fuchsia::net::interfaces::Properties properties;
    properties.set_id(interface.id);
    properties.set_name(interface.name);
    properties.set_has_default_ipv4_route(true);
    properties.set_has_default_ipv6_route(true);
    event = fuchsia::net::interfaces::Event::WithExisting(std::move(properties));
    existing_events_.push_back(std::move(event));
  }

  async_dispatcher_t* dispatcher_;
  fuchsia::net::interfaces::Watcher::WatchCallback watch_callback_;
  std::deque<fuchsia::net::interfaces::Event> events_;
  std::vector<fuchsia::net::interfaces::Event> existing_events_;
  fidl::Binding<fuchsia::net::interfaces::State> state_binding_{this};
  fidl::Binding<fuchsia::net::interfaces::Watcher> watcher_binding_{this};
};

class FakeLowpanDeviceRoute final : public fuchsia::lowpan::device::testing::DeviceRoute_TestBase {
 public:
  void NotImplemented_(const std::string& name) override { FAIL() << "Not implemented: " << name; }

  void RegisterOnMeshPrefix(fuchsia::lowpan::device::OnMeshPrefix prefix,
                            RegisterOnMeshPrefixCallback callback) override {
    EXPECT_EQ(prefix.default_route_preference(), fuchsia::lowpan::device::RoutePreference::MEDIUM);
    EXPECT_TRUE(prefix.stable());
    EXPECT_TRUE(prefix.slaac_preferred());
    EXPECT_TRUE(prefix.slaac_valid());
    on_mesh_prefixes_.push_back(std::move(prefix));
    callback();
  }

  void UnregisterOnMeshPrefix(fuchsia::lowpan::Ipv6Subnet subnet,
                              RegisterOnMeshPrefixCallback callback) override {
    auto it = std::remove_if(
        on_mesh_prefixes_.begin(), on_mesh_prefixes_.end(), [&](const OnMeshPrefix& prefix) {
          return std::memcmp(subnet.addr.addr.data(), prefix.subnet().addr.addr.data(),
                             subnet.addr.addr.size()) == 0;
        });
    on_mesh_prefixes_.erase(it);
    callback();
  }

  bool ContainsSubnetForAddress(Inet::IPAddress address) {
    auto it = std::find_if(
        on_mesh_prefixes_.begin(), on_mesh_prefixes_.end(), [&](const OnMeshPrefix& prefix) {
          return std::memcmp(prefix.subnet().addr.addr.data(), (uint8_t*)address.Addr,
                             prefix.subnet().addr.addr.size()) == 0;
        });
    return it != on_mesh_prefixes_.end();
  }

 private:
  std::vector<OnMeshPrefix> on_mesh_prefixes_;
};

class FakeLowpanLookup final : public fuchsia::lowpan::device::testing::Lookup_TestBase {
 public:
  void NotImplemented_(const std::string& name) override { FAIL() << "Not implemented: " << name; }

  void GetDevices(GetDevicesCallback callback) override {
    callback({TestThreadStackManager::kThreadInterfaceName});
  }

  void LookupDevice(std::string name, Protocols protocols, LookupDeviceCallback callback) override {
    Lookup_LookupDevice_Result result;
    if (name != TestThreadStackManager::kThreadInterfaceName) {
      result.set_err(ServiceError::DEVICE_NOT_FOUND);
      callback(std::move(result));
      return;
    }

    Lookup_LookupDevice_Response response;
    if (protocols.has_device_route()) {
      device_route_bindings_.AddBinding(&device_route_,
                                        std::move(*protocols.mutable_device_route()), dispatcher_);
    }

    result.set_response(response);
    callback(std::move(result));
  }

  fidl::InterfaceRequestHandler<Lookup> GetHandler(async_dispatcher_t* dispatcher) {
    dispatcher_ = dispatcher;
    return [this](fidl::InterfaceRequest<Lookup> request) {
      binding_.Bind(std::move(request), dispatcher_);
    };
  }

  FakeLowpanDeviceRoute& device_route() { return device_route_; }

 private:
  FakeLowpanDeviceRoute device_route_;
  fidl::BindingSet<DeviceRoute> device_route_bindings_;
  async_dispatcher_t* dispatcher_;
  fidl::Binding<Lookup> binding_{this};
};

// Fake implementation of the fuchsia::netstack::Netstack that provides
// the minimal functionality required for WARM to run.
class FakeNetstack : public fuchsia::netstack::testing::Netstack_TestBase,
                     public fuchsia::netstack::testing::RouteTableTransaction_TestBase {
 private:
  void NotImplemented_(const std::string& name) override { FAIL() << "Not implemented: " << name; }

  void SetInterfaceAddress(uint32_t nicid, ::fuchsia::net::IpAddress addr, uint8_t prefix_len,
                           SetInterfaceAddressCallback callback) override {
    // Confirm that the configured address is a V6 address.
    ASSERT_TRUE(addr.is_ipv6());

    // Find the interface with the specified ID and append the address.
    auto it = std::find_if(interfaces_.begin(), interfaces_.end(),
                           [&](const OwnedInterface& interface) { return nicid == interface.id; });
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
                           [&](const OwnedInterface& interface) { return nicid == interface.id; });
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

  void AddRoute(::fuchsia::netstack::RouteTableEntry route_table_entry,
                AddRouteCallback callback) override {
    route_table_.push_back(std::move(route_table_entry));
    callback(ZX_OK);
  }

  void DelRoute(::fuchsia::netstack::RouteTableEntry route_table_entry,
                DelRouteCallback callback) override {
    auto it = std::remove_if(
        route_table_.begin(), route_table_.end(),
        [&](const ::fuchsia::netstack::RouteTableEntry& entry) {
          return route_table_entry.nicid == entry.nicid &&
                 route_table_entry.metric == entry.metric &&
                 CompareIpAddress(route_table_entry.destination.addr, entry.destination.addr) &&
                 route_table_entry.destination.prefix_len == entry.destination.prefix_len;
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
  FakeNetstack& AddOwnedInterface(std::string name) {
    interfaces_.push_back({
        .id = ++last_id_assigned,
        .name = name,
    });
    return *this;
  }

  // Remove the fake interface with the given name. If it is not present, no change occurs.
  FakeNetstack& RemoveOwnedInterface(std::string name) {
    auto it =
        std::remove_if(interfaces_.begin(), interfaces_.end(),
                       [&](const OwnedInterface& interface) { return interface.name == name; });
    interfaces_.erase(it, interfaces_.end());
    return *this;
  }

  // Access the current interfaces.
  const std::vector<OwnedInterface>& interfaces() const { return interfaces_; }

  // Access the current route table.
  const std::vector<RouteTableEntry>& route_table() const { return route_table_; }

  // Get a pointer to an interface by name.
  OwnedInterface& GetInterfaceByName(const std::string name) {
    auto it = std::find_if(interfaces_.begin(), interfaces_.end(),
                           [&](const OwnedInterface& interface) { return interface.name == name; });
    EXPECT_NE(it, interfaces_.end());
    return *it;
  }

  // Check if the given interface ID and address exists in the route table.
  bool FindRouteTableEntry(uint32_t nicid, ::nl::Inet::IPAddress addr,
                           uint32_t metric = kRouteMetric_HighPriority) {
    auto it = std::find_if(
        route_table_.begin(), route_table_.end(), [&](const RouteTableEntry& route_table_entry) {
          return nicid == route_table_entry.nicid && metric == route_table_entry.metric &&
                 CompareIpAddress(addr, route_table_entry.destination.addr);
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
  std::vector<OwnedInterface> interfaces_;
  std::vector<RouteTableEntry> route_table_;
  uint32_t last_id_assigned = 0;
};

// Fake implementation of he fuchsia::net::stack::Stack and that provides
// the minimal functionality required for WARM to run.
class FakeStack : public fuchsia::net::stack::testing::Stack_TestBase {
 private:
  void NotImplemented_(const std::string& name) override { FAIL() << "Not implemented: " << name; }

  // fuchsia::net::stack::Stack interface definitions.
  void SetInterfaceIpForwarding(uint64_t id, fuchsia::net::IpVersion ip_version, bool enabled,
                                SetInterfaceIpForwardingCallback callback) override {
    fuchsia::net::stack::Stack_SetInterfaceIpForwarding_Result result;
    fuchsia::net::stack::Stack_SetInterfaceIpForwarding_Response response;

    EXPECT_EQ(ip_version, fuchsia::net::IpVersion::V6);
    EXPECT_TRUE(enabled);

    if (forwarding_success_) {
      ip_forwarded_interfaces_.push_back(id);
      result.set_response(std::move(response));
    } else {
      result.set_err(fuchsia::net::stack::Error::INTERNAL);
    }
    callback(std::move(result));
  }

 public:
  // Mutators, accessors, and helpers for tests.

  // Set the success of an IP forwarding request.
  void SetForwardingSuccess(bool forwarding_success) { forwarding_success_ = forwarding_success; }

  // Check if interface is forwarded.
  bool IsInterfaceForwarded(uint64_t id) {
    return std::find(ip_forwarded_interfaces_.begin(), ip_forwarded_interfaces_.end(), id) !=
           ip_forwarded_interfaces_.end();
  }

  fidl::InterfaceRequestHandler<fuchsia::net::stack::Stack> GetHandler(
      async_dispatcher_t* dispatcher) {
    dispatcher_ = dispatcher;
    return [this](fidl::InterfaceRequest<fuchsia::net::stack::Stack> request) {
      binding_.Bind(std::move(request), dispatcher_);
    };
  }

 private:
  fidl::Binding<fuchsia::net::stack::Stack> binding_{this};
  async_dispatcher_t* dispatcher_;
  std::vector<uint64_t> ip_forwarded_interfaces_;
  bool forwarding_success_ = true;
};

class WarmTest : public testing::WeaveTestFixture<> {
 public:
  void SetUp() override {
    WeaveTestFixture<>::SetUp();

    // Initialize everything needed for the test.
    context_provider_.service_directory_provider()->AddService(
        fake_lowpan_lookup_.GetHandler(dispatcher()));
    context_provider_.service_directory_provider()->AddService(
        fake_net_interfaces_.GetHandler(dispatcher()));
    context_provider_.service_directory_provider()->AddService(
        fake_net_stack_.GetHandler(dispatcher()));
    context_provider_.service_directory_provider()->AddService(
        fake_stack_.GetHandler(dispatcher()));

    PlatformMgrImpl().SetComponentContextForProcess(context_provider_.TakeContext());
    ConnectivityMgrImpl().SetDelegate(std::make_unique<TestConnectivityManager>());
    ThreadStackMgrImpl().SetDelegate(std::make_unique<TestThreadStackManager>());
    Warm::Platform::Init(nullptr);

    // Report that thread is provisioned by default, so that WARM does not
    // always reject add-operations due to lack of provisioning.
    thread_delegate().set_is_thread_provisioned(true);

    // Populate initial fake interfaces
    AddOwnedInterface(kTunInterfaceName);
    AddOwnedInterface(TestThreadStackManager::kThreadInterfaceName);
    AddOwnedInterface(TestConnectivityManager::kWiFiInterfaceName);

    RunFixtureLoop();
  }

  void TearDown() override {
    StopFixtureLoop();
    ConnectivityMgrImpl().SetDelegate(nullptr);
    ThreadStackMgrImpl().SetDelegate(nullptr);
    WeaveTestFixture<>::TearDown();
  }

 protected:
  FakeLowpanLookup& fake_lowpan_lookup() { return fake_lowpan_lookup_; }
  FakeNetInterfaces& fake_net_interfaces() { return fake_net_interfaces_; }
  FakeNetstack& fake_net_stack() { return fake_net_stack_; }
  FakeStack& fake_stack() { return fake_stack_; }

  TestThreadStackManager& thread_delegate() {
    return *reinterpret_cast<TestThreadStackManager*>(ThreadStackMgrImpl().GetDelegate());
  }

  void AddOwnedInterface(std::string name) {
    fake_net_stack_.AddOwnedInterface(name);
    fake_net_interfaces_.InitializeInterfaces(fake_net_stack_.interfaces());
  }

  void RemoveOwnedInterface(std::string name) {
    fake_net_stack_.RemoveOwnedInterface(name);
    fake_net_interfaces_.InitializeInterfaces(fake_net_stack_.interfaces());
  }

  OwnedInterface& GetThreadInterface() {
    return fake_net_stack_.GetInterfaceByName(TestThreadStackManager::kThreadInterfaceName);
  }

  uint32_t GetThreadInterfaceId() { return GetThreadInterface().id; }

  OwnedInterface& GetTunnelInterface() {
    return fake_net_stack_.GetInterfaceByName(kTunInterfaceName);
  }

  uint32_t GetTunnelInterfaceId() { return GetTunnelInterface().id; }

  OwnedInterface& GetWiFiInterface() {
    return fake_net_stack_.GetInterfaceByName(TestConnectivityManager::kWiFiInterfaceName);
  }

  uint32_t GetWiFiInterfaceId() { return GetWiFiInterface().id; }

 private:
  FakeLowpanLookup fake_lowpan_lookup_;
  FakeNetstack fake_net_stack_;
  FakeNetInterfaces fake_net_interfaces_;
  FakeStack fake_stack_;
  sys::testing::ComponentContextProvider context_provider_;
};

TEST_F(WarmTest, AddRemoveAddressThread) {
  constexpr char kSubnetIp[] = "2001:0DB8:0042::";
  constexpr uint8_t kPrefixLength = 48;
  Inet::IPAddress addr;

  // Sanity check - no addresses assigned.
  OwnedInterface& lowpan = GetThreadInterface();
  EXPECT_EQ(lowpan.ipv6addrs.size(), 0u);

  // Attempt to add the address.
  ASSERT_TRUE(Inet::IPAddress::FromString(kSubnetIp, addr));
  auto result = AddRemoveHostAddress(kInterfaceTypeThread, addr, kPrefixLength, /*add*/ true);
  EXPECT_EQ(result, kPlatformResultSuccess);

  // Confirm that it worked.
  ASSERT_EQ(lowpan.ipv6addrs.size(), 1u);
  EXPECT_TRUE(CompareIpAddress(addr, lowpan.ipv6addrs[0].addr));
  EXPECT_TRUE(fake_lowpan_lookup().device_route().ContainsSubnetForAddress(addr));

  // Attempt to remove the address.
  result = AddRemoveHostAddress(kInterfaceTypeThread, addr, kPrefixLength, /*add*/ false);
  EXPECT_EQ(result, kPlatformResultSuccess);

  // Confirm that it worked.
  EXPECT_EQ(lowpan.ipv6addrs.size(), 0u);
  EXPECT_FALSE(fake_lowpan_lookup().device_route().ContainsSubnetForAddress(addr));
}

TEST_F(WarmTest, AddAddressThreadUnprovisioned) {
  constexpr char kSubnetIp[] = "2001:0DB8:0042::";
  constexpr uint8_t kPrefixLength = 48;
  Inet::IPAddress addr;

  // Fake unprovisioned TSM.
  thread_delegate().set_is_thread_provisioned(false);

  // Sanity check - no addresses assigned.
  OwnedInterface& lowpan = GetThreadInterface();
  EXPECT_EQ(lowpan.ipv6addrs.size(), 0u);

  // Attempt to add the address.
  ASSERT_TRUE(Inet::IPAddress::FromString(kSubnetIp, addr));
  auto result = AddRemoveHostAddress(kInterfaceTypeThread, addr, kPrefixLength, /*add*/ true);
  EXPECT_EQ(result, kPlatformResultFailure);

  // Confirm that nothing was changed.
  EXPECT_EQ(lowpan.ipv6addrs.size(), 0u);
  EXPECT_FALSE(fake_lowpan_lookup().device_route().ContainsSubnetForAddress(addr));
}

TEST_F(WarmTest, AddRemoveAddressTunnel) {
  constexpr char kSubnetIp[] = "2001:0DB8:0042::";
  constexpr uint8_t kPrefixLength = 48;
  Inet::IPAddress addr;

  // Sanity check - no addresses assigned.
  OwnedInterface& weave_tun = GetTunnelInterface();
  EXPECT_EQ(weave_tun.ipv6addrs.size(), 0u);

  // Attempt to add the address.
  ASSERT_TRUE(Inet::IPAddress::FromString(kSubnetIp, addr));
  auto result = AddRemoveHostAddress(kInterfaceTypeTunnel, addr, kPrefixLength, /*add*/ true);
  EXPECT_EQ(result, kPlatformResultSuccess);

  // Confirm that it worked.
  ASSERT_EQ(weave_tun.ipv6addrs.size(), 1u);
  EXPECT_TRUE(CompareIpAddress(addr, weave_tun.ipv6addrs[0].addr));

  // Attempt to remove the address.
  result = AddRemoveHostAddress(kInterfaceTypeTunnel, addr, kPrefixLength, /*add*/ false);
  EXPECT_EQ(result, kPlatformResultSuccess);

  // Confirm that it worked.
  EXPECT_EQ(weave_tun.ipv6addrs.size(), 0u);
}

TEST_F(WarmTest, AddRemoveAddressWiFi) {
  constexpr char kSubnetIp[] = "2001:0DB8:0042::";
  constexpr uint8_t kPrefixLength = 48;
  Inet::IPAddress addr;

  // Sanity check - no addresses assigned.
  OwnedInterface& wlan = GetWiFiInterface();
  EXPECT_EQ(wlan.ipv6addrs.size(), 0u);

  // Attempt to add the address.
  ASSERT_TRUE(Inet::IPAddress::FromString(kSubnetIp, addr));
  auto result = AddRemoveHostAddress(kInterfaceTypeWiFi, addr, kPrefixLength, /*add*/ true);
  EXPECT_EQ(result, kPlatformResultSuccess);

  // Confirm that it worked.
  ASSERT_EQ(wlan.ipv6addrs.size(), 1u);
  EXPECT_TRUE(CompareIpAddress(addr, wlan.ipv6addrs[0].addr));

  // Attempt to remove the address.
  result = AddRemoveHostAddress(kInterfaceTypeWiFi, addr, kPrefixLength, /*add*/ false);
  EXPECT_EQ(result, kPlatformResultSuccess);

  // Confirm that it worked.
  EXPECT_EQ(wlan.ipv6addrs.size(), 0u);
}

TEST_F(WarmTest, RemoveAddressThreadNotFound) {
  constexpr char kSubnetIp[] = "2001:0DB8:0042::";
  constexpr uint8_t kPrefixLength = 48;
  Inet::IPAddress addr;

  // Sanity check - no addresses assigned.
  OwnedInterface& lowpan = GetThreadInterface();
  EXPECT_EQ(lowpan.ipv6addrs.size(), 0u);

  // Attempt to remove the address, expecting failure.
  ASSERT_TRUE(Inet::IPAddress::FromString(kSubnetIp, addr));
  auto result = AddRemoveHostAddress(kInterfaceTypeThread, addr, kPrefixLength, /*add*/ false);
  EXPECT_EQ(result, kPlatformResultFailure);

  // Sanity check - still no addresses assigned.
  EXPECT_EQ(lowpan.ipv6addrs.size(), 0u);
  EXPECT_FALSE(fake_lowpan_lookup().device_route().ContainsSubnetForAddress(addr));
}

TEST_F(WarmTest, RemoveAddressTunnelNotFound) {
  constexpr char kSubnetIp[] = "2001:0DB8:0042::";
  constexpr uint8_t kPrefixLength = 48;
  Inet::IPAddress addr;

  // Sanity check - no addresses assigned.
  OwnedInterface& weave_tun = GetTunnelInterface();
  EXPECT_EQ(weave_tun.ipv6addrs.size(), 0u);

  // Attempt to remove the address, expecting failure.
  ASSERT_TRUE(Inet::IPAddress::FromString(kSubnetIp, addr));
  auto result = AddRemoveHostAddress(kInterfaceTypeTunnel, addr, kPrefixLength, /*add*/ false);
  EXPECT_EQ(result, kPlatformResultFailure);

  // Sanity check - still no addresses assigned.
  EXPECT_EQ(weave_tun.ipv6addrs.size(), 0u);
}

TEST_F(WarmTest, RemoveAddressWiFiNotFound) {
  constexpr char kSubnetIp[] = "2001:0DB8:0042::";
  constexpr uint8_t kPrefixLength = 48;
  Inet::IPAddress addr;

  // Sanity check - no addresses assigned.
  OwnedInterface& wlan = GetWiFiInterface();
  EXPECT_EQ(wlan.ipv6addrs.size(), 0u);

  // Attempt to remove the address, expecting failure.
  ASSERT_TRUE(Inet::IPAddress::FromString(kSubnetIp, addr));
  auto result = AddRemoveHostAddress(kInterfaceTypeWiFi, addr, kPrefixLength, /*add*/ false);
  EXPECT_EQ(result, kPlatformResultFailure);

  // Sanity check - still no addresses assigned.
  EXPECT_EQ(wlan.ipv6addrs.size(), 0u);
}

TEST_F(WarmTest, AddAddressThreadNoInterface) {
  constexpr char kSubnetIp[] = "2001:0DB8:0042::";
  constexpr uint8_t kPrefixLength = 48;
  Inet::IPAddress addr;

  RemoveOwnedInterface(TestThreadStackManager::kThreadInterfaceName);

  // Attempt to add to the interface when there's no Thread interface. Expect failure.
  ASSERT_TRUE(Inet::IPAddress::FromString(kSubnetIp, addr));
  auto result = AddRemoveHostAddress(kInterfaceTypeThread, addr, kPrefixLength, /*add*/ true);
  EXPECT_EQ(result, kPlatformResultFailure);
  EXPECT_FALSE(fake_lowpan_lookup().device_route().ContainsSubnetForAddress(addr));
}

TEST_F(WarmTest, RemoveAddressThreadNoInterface) {
  constexpr char kSubnetIp[] = "2001:0DB8:0042::";
  constexpr uint8_t kPrefixLength = 48;
  Inet::IPAddress addr;

  RemoveOwnedInterface(TestThreadStackManager::kThreadInterfaceName);

  // Attempt to remove from the interface when there's no Thread interface. Expect success.
  ASSERT_TRUE(Inet::IPAddress::FromString(kSubnetIp, addr));
  auto result = AddRemoveHostAddress(kInterfaceTypeThread, addr, kPrefixLength, /*add*/ false);
  EXPECT_EQ(result, kPlatformResultSuccess);
  EXPECT_FALSE(fake_lowpan_lookup().device_route().ContainsSubnetForAddress(addr));
}

TEST_F(WarmTest, AddAddressTunnelNoInterface) {
  constexpr char kSubnetIp[] = "2001:0DB8:0042::";
  constexpr uint8_t kPrefixLength = 48;
  Inet::IPAddress addr;

  RemoveOwnedInterface(kTunInterfaceName);

  // Attempt to add to the interface when there's no Tunnel interface. Expect failure.
  ASSERT_TRUE(Inet::IPAddress::FromString(kSubnetIp, addr));
  auto result = AddRemoveHostAddress(kInterfaceTypeTunnel, addr, kPrefixLength, /*add*/ true);
  EXPECT_EQ(result, kPlatformResultFailure);
}

TEST_F(WarmTest, RemoveAddressTunnelNoInterface) {
  constexpr char kSubnetIp[] = "2001:0DB8:0042::";
  constexpr uint8_t kPrefixLength = 48;
  Inet::IPAddress addr;

  RemoveOwnedInterface(kTunInterfaceName);

  // Attempt to remove from the interface when there's no Tunnel interface. Expect success.
  ASSERT_TRUE(Inet::IPAddress::FromString(kSubnetIp, addr));
  auto result = AddRemoveHostAddress(kInterfaceTypeTunnel, addr, kPrefixLength, /*add*/ false);
  EXPECT_EQ(result, kPlatformResultSuccess);
}

TEST_F(WarmTest, AddAddressWiFiNoInterface) {
  constexpr char kSubnetIp[] = "2001:0DB8:0042::";
  constexpr uint8_t kPrefixLength = 48;
  Inet::IPAddress addr;

  RemoveOwnedInterface(TestConnectivityManager::kWiFiInterfaceName);

  // Attempt to add to the interface when there's no WiFi interface. Expect failure.
  ASSERT_TRUE(Inet::IPAddress::FromString(kSubnetIp, addr));
  auto result = AddRemoveHostAddress(kInterfaceTypeWiFi, addr, kPrefixLength, /*add*/ true);
  EXPECT_EQ(result, kPlatformResultFailure);
}

TEST_F(WarmTest, RemoveAddressWiFiNoInterface) {
  constexpr char kSubnetIp[] = "2001:0DB8:0042::";
  constexpr uint8_t kPrefixLength = 48;
  Inet::IPAddress addr;

  RemoveOwnedInterface(TestConnectivityManager::kWiFiInterfaceName);

  // Attempt to remove from the interface when there's no WiFi interface. Expect success.
  ASSERT_TRUE(Inet::IPAddress::FromString(kSubnetIp, addr));
  auto result = AddRemoveHostAddress(kInterfaceTypeWiFi, addr, kPrefixLength, /*add*/ false);
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

  // Confirm that this interface is now forwarded.
  EXPECT_TRUE(fake_stack().IsInterfaceForwarded(thread_iface_id));

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

  // Confirm that this interface is now forwarded.
  EXPECT_TRUE(fake_stack().IsInterfaceForwarded(tunnel_iface_id));

  // Remove the route to the Tunnel interface.
  result = AddRemoveHostRoute(kInterfaceTypeTunnel, prefix, kRoutePriorityHigh, /*add*/ false);
  EXPECT_EQ(result, kPlatformResultSuccess);

  // Confirm that the removal worked.
  EXPECT_FALSE(fake_net_stack().FindRouteTableEntry(tunnel_iface_id, prefix.IPAddr));
}

TEST_F(WarmTest, AddRemoveHostRouteWiFi) {
  constexpr char kSubnetIp[] = "2001:0DB8:0042::";
  constexpr uint8_t kPrefixLength = 48;
  Inet::IPPrefix prefix;

  ASSERT_TRUE(Inet::IPAddress::FromString(kSubnetIp, prefix.IPAddr));
  prefix.Length = kPrefixLength;

  // Sanity check - confirm no routes to the WiFi interface exist.
  uint64_t wlan_iface_id = GetWiFiInterfaceId();
  ASSERT_NE(wlan_iface_id, 0u);
  EXPECT_FALSE(fake_net_stack().FindRouteTableEntry(wlan_iface_id, prefix.IPAddr));

  // Attempt to add a route to the WiFi interface.
  auto result = AddRemoveHostRoute(kInterfaceTypeWiFi, prefix, kRoutePriorityHigh, /*add*/ true);
  EXPECT_EQ(result, kPlatformResultSuccess);

  // Confirm that a route exists to the WiFi interface with the given IP.
  EXPECT_TRUE(fake_net_stack().FindRouteTableEntry(wlan_iface_id, prefix.IPAddr));

  // Confirm that this interface is NOT forwarded.
  EXPECT_FALSE(fake_stack().IsInterfaceForwarded(wlan_iface_id));

  // Remove the route to the WiFi interface.
  result = AddRemoveHostRoute(kInterfaceTypeWiFi, prefix, kRoutePriorityHigh, /*add*/ false);
  EXPECT_EQ(result, kPlatformResultSuccess);

  // Confirm that the removal worked.
  EXPECT_FALSE(fake_net_stack().FindRouteTableEntry(wlan_iface_id, prefix.IPAddr));
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

  // Confirm that the interface is not forwarded.
  EXPECT_FALSE(fake_stack().IsInterfaceForwarded(thread_iface_id));

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

  // Confirm that the interface is not forwarded.
  EXPECT_FALSE(fake_stack().IsInterfaceForwarded(tunnel_iface_id));

  // Sanity check - confirm still no routes to the Tunnel interface exist.
  EXPECT_FALSE(fake_net_stack().FindRouteTableEntry(tunnel_iface_id, prefix.IPAddr));
}

TEST_F(WarmTest, RemoveHostRouteWiFiNotFound) {
  constexpr char kSubnetIp[] = "2001:0DB8:0042::";
  constexpr uint8_t kPrefixLength = 48;
  Inet::IPPrefix prefix;

  ASSERT_TRUE(Inet::IPAddress::FromString(kSubnetIp, prefix.IPAddr));
  prefix.Length = kPrefixLength;

  // Sanity check - confirm no routes to the WiFi interface exist.
  uint64_t wlan_iface_id = GetWiFiInterfaceId();
  ASSERT_NE(wlan_iface_id, 0u);
  EXPECT_FALSE(fake_net_stack().FindRouteTableEntry(wlan_iface_id, prefix.IPAddr));

  // Remove the non-existent route to the WiFi interface, expect failure.
  auto result = AddRemoveHostRoute(kInterfaceTypeWiFi, prefix, kRoutePriorityHigh, /*add*/ false);
  EXPECT_EQ(result, kPlatformResultFailure);

  // Confirm that the interface is not forwarded.
  EXPECT_FALSE(fake_stack().IsInterfaceForwarded(wlan_iface_id));

  // Sanity check - confirm still no routes to the WiFi interface exist.
  EXPECT_FALSE(fake_net_stack().FindRouteTableEntry(wlan_iface_id, prefix.IPAddr));
}

TEST_F(WarmTest, AddHostRouteThreadForwardingFailure) {
  constexpr char kSubnetIp[] = "2001:0DB8:0042::";
  constexpr uint8_t kPrefixLength = 48;
  Inet::IPPrefix prefix;

  ASSERT_TRUE(Inet::IPAddress::FromString(kSubnetIp, prefix.IPAddr));
  prefix.Length = kPrefixLength;

  // Sanity check - confirm no routes to the Thread interface exist.
  uint64_t thread_iface_id = GetThreadInterfaceId();
  ASSERT_NE(thread_iface_id, 0u);
  EXPECT_FALSE(fake_net_stack().FindRouteTableEntry(thread_iface_id, prefix.IPAddr));

  // Simulate a forwarding failure.
  fake_stack().SetForwardingSuccess(false);

  // Attempt to add a route to the Thread interface, expect failure.
  auto result = AddRemoveHostRoute(kInterfaceTypeThread, prefix, kRoutePriorityHigh, /*add*/ true);
  EXPECT_EQ(result, kPlatformResultFailure);

  // Confirm that a route exists to the Thread interface with the given IP.
  EXPECT_TRUE(fake_net_stack().FindRouteTableEntry(thread_iface_id, prefix.IPAddr));

  // Confirm that this interface is not forwarded.
  EXPECT_FALSE(fake_stack().IsInterfaceForwarded(thread_iface_id));
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
