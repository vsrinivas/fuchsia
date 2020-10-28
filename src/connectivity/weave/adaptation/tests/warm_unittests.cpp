
// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/net/cpp/fidl.h>
#include <fuchsia/net/stack/cpp/fidl_test_base.h>
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
using DeviceLayer::PlatformMgrImpl;
using DeviceLayer::ThreadStackMgrImpl;
using DeviceLayer::Internal::testing::WeaveTestFixture;

using fuchsia::net::Subnet;
using fuchsia::net::stack::AdministrativeStatus;
using fuchsia::net::stack::ForwardingEntry;
using fuchsia::net::stack::InterfaceInfo;
using fuchsia::net::stack::PhysicalStatus;
using fuchsia::net::stack::Stack_AddForwardingEntry_Response;
using fuchsia::net::stack::Stack_AddForwardingEntry_Result;
using fuchsia::net::stack::Stack_AddInterfaceAddress_Response;
using fuchsia::net::stack::Stack_AddInterfaceAddress_Result;
using fuchsia::net::stack::Stack_DelForwardingEntry_Response;
using fuchsia::net::stack::Stack_DelForwardingEntry_Result;
using fuchsia::net::stack::Stack_DelInterfaceAddress_Response;
using fuchsia::net::stack::Stack_DelInterfaceAddress_Result;

// This is the OpenWeave IP address, not to be confused with the similar IpAddress from Fuchsia.
using Inet::IPAddress;
using Inet::IPPrefix;

constexpr char kTunInterfaceName[] = "weav-tun0";
constexpr char kThreadInterfaceName[] = "thread0";
constexpr size_t kIpAddressSize = 16;

// Copies the IP address bytes in network order from a Weave Inet::IPAddress to a std::array.
std::array<uint8_t, kIpAddressSize> WeaveIpAddressToArray(const IPAddress& addr) {
  std::array<uint8_t, kIpAddressSize> array;
  const uint8_t* data = reinterpret_cast<const uint8_t*>(addr.Addr);
  std::copy(data, data + kIpAddressSize, array.begin());
  return array;
}
}  // namespace

// Fake implementation of TSM delegate, only provides an interface name.
class FakeThreadStackManagerDelegate : public DeviceLayer::ThreadStackManagerDelegateImpl {
 public:
  WEAVE_ERROR InitThreadStack() override {
    interface_name_ = kThreadInterfaceName;
    return WEAVE_NO_ERROR;
  }

  const std::string& GetInterfaceName() const override { return interface_name_; }

 private:
  std::string interface_name_;
};

// Fake implementation of fuchsia::net::stack::Stack that only provides the fake functionality
// needed for WARM.
class FakeNetStack : public fuchsia::net::stack::testing::Stack_TestBase {
 private:
  void NotImplemented_(const std::string& name) override { FAIL() << "Not implemented: " << name; }

  // FIDL interface definitions

  void ListInterfaces(ListInterfacesCallback callback) override {
    std::vector<InterfaceInfo> result{interfaces_.size()};

    for (size_t i = 0; i < result.size(); ++i) {
      if (interfaces_[i].Clone(&result[i]) != ZX_OK) {
        ADD_FAILURE() << "InterfaceInfo::Clone() failed.";
      };
    }

    callback(std::move(result));
  }

  void AddInterfaceAddress(uint64_t interface_id, Subnet ifaddr,
                           AddInterfaceAddressCallback callback) override {
    Stack_AddInterfaceAddress_Result result;
    Stack_AddInterfaceAddress_Response response;

    // Interface ID 0 is always invalid.
    if (interface_id == 0) {
      result.set_err(fuchsia::net::stack::Error::INVALID_ARGS);
      callback(std::move(result));
      return;
    }

    // Find the interface with the specified ID.
    for (auto& interface : interfaces_) {
      if (interface_id == interface.id) {
        interface.properties.addresses.push_back(std::move(ifaddr));
        result.set_response(std::move(response));
        callback(std::move(result));
        return;
      }
    }

    // No interface was found with the given ID.
    result.set_err(fuchsia::net::stack::Error::NOT_FOUND);
    callback(std::move(result));
  }

  void DelInterfaceAddress(uint64_t interface_id, Subnet ifaddr,
                           DelInterfaceAddressCallback callback) override {
    Stack_DelInterfaceAddress_Result result;
    Stack_DelInterfaceAddress_Response response;

    // Interface ID 0 is always invalid.
    if (interface_id == 0) {
      result.set_err(fuchsia::net::stack::Error::INVALID_ARGS);
      callback(std::move(result));
      return;
    }

    // Search the interfaces for the specified ID.
    for (auto& interface : interfaces_) {
      auto& addrs = interface.properties.addresses;
      if (interface_id == interface.id) {
        // Find the specified address.
        auto it = std::find_if(addrs.cbegin(), addrs.cend(),
                               [&](const Subnet& addr) { return fidl::Equals(ifaddr, addr); });

        // No matching address was found.
        if (it == addrs.cend()) {
          result.set_err(fuchsia::net::stack::Error::NOT_FOUND);
          callback(std::move(result));
          return;
        }

        // Remove the address.
        addrs.erase(it);
        result.set_response(std::move(response));
        callback(std::move(result));
        return;
      }
    }

    // No interface was found with the given ID.
    result.set_err(fuchsia::net::stack::Error::NOT_FOUND);
    callback(std::move(result));
  }

  void AddForwardingEntry(ForwardingEntry entry, AddForwardingEntryCallback callback) override {
    Stack_AddForwardingEntry_Result result;
    Stack_AddForwardingEntry_Response response;

    // Interface ID 0 is always invalid.
    if (entry.destination.is_device_id() && entry.destination.device_id() == 0) {
      result.set_err(fuchsia::net::stack::Error::INVALID_ARGS);
      callback(std::move(result));
      return;
    }

    // Check if there's an existing entry for this subnet.
    auto it =
        std::find_if(fwd_table_.cbegin(), fwd_table_.cend(), [&](const ForwardingEntry& existing) {
          return fidl::Equals(existing.subnet, entry.subnet);
        });
    if (it != fwd_table_.cend()) {
      result.set_err(fuchsia::net::stack::Error::ALREADY_EXISTS);
      callback(std::move(result));
      return;
    }

    // Add to the forwarding table.
    fwd_table_.push_back(std::move(entry));
    result.set_response(std::move(response));
    callback(std::move(result));
  }

  void DelForwardingEntry(Subnet subnet, DelForwardingEntryCallback callback) override {
    Stack_DelForwardingEntry_Result result;
    Stack_DelForwardingEntry_Response response;

    // Search for the entry with the given subnet.
    auto it = std::find_if(
        fwd_table_.cbegin(), fwd_table_.cend(),
        [&](const ForwardingEntry& existing) { return fidl::Equals(existing.subnet, subnet); });
    if (it == fwd_table_.cend()) {
      result.set_err(fuchsia::net::stack::Error::NOT_FOUND);
      callback(std::move(result));
      return;
    }

    // Delete the entry.
    fwd_table_.erase(it);
    result.set_response(std::move(response));
    callback(std::move(result));
  }

 public:
  // Mutators, accessors, and helpers for tests.

  // Add a fake interface with the given name. Does not check for duplicates.
  FakeNetStack& AddFakeInterface(std::string name) {
    if (name == "") {
      ADD_FAILURE() << "Invalid name supplied in test data.";
    }
    interfaces_.push_back(InterfaceInfo{
        .id = ++last_id_assigned,
        .properties =
            {
                .name = std::move(name),
                .administrative_status = AdministrativeStatus::DISABLED,
                .physical_status = PhysicalStatus::DOWN,
            },
    });
    return *this;
  }

  // Remove the fake interface with the given name. If it is not present, no change occurs.
  FakeNetStack& RemoveFakeInterface(std::string name) {
    auto it = std::find_if(
        interfaces_.cbegin(), interfaces_.cend(),
        [&](const InterfaceInfo& interface) { return interface.properties.name == name; });
    if (it != interfaces_.cend()) {
      interfaces_.erase(it);
    }
    return *this;
  }

  // Access the current interfaces.
  const std::vector<InterfaceInfo>& interfaces() const { return interfaces_; }

  // Access the current forwarding table.
  const std::vector<ForwardingEntry>& fwd_table() const { return fwd_table_; }

  // Get a pointer to an interface by name. Returns nullptr if not found.
  const InterfaceInfo* GetInterface(const std::string& name) const {
    auto it = std::find_if(
        interfaces_.cbegin(), interfaces_.cend(),
        [&](const InterfaceInfo& interface) { return interface.properties.name == name; });

    if (it != interfaces_.cend()) {
      return &(*it);
    } else {
      return nullptr;
    }
  }

  // Get a pointer to the first forwarding entry that meets the given predicate. Returns nullptr if
  // no entries met the predicate.
  const ForwardingEntry* FindForwardingEntry(fit::function<bool(const ForwardingEntry&)> pred) {
    auto it = std::find_if(fwd_table_.cbegin(), fwd_table_.cend(), std::move(pred));
    if (it != fwd_table_.cend()) {
      return &(*it);
    } else {
      return nullptr;
    }
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
  std::vector<InterfaceInfo> interfaces_;
  std::vector<ForwardingEntry> fwd_table_;
  uint64_t last_id_assigned = 0;
};

class WarmTest : public WeaveTestFixture {
 public:
  void SetUp() override {
    WeaveTestFixture::SetUp();

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
    WeaveTestFixture::TearDown();
  }

 protected:
  FakeNetStack& fake_net_stack() { return fake_net_stack_; }

  const InterfaceInfo* GetThreadInterface() {
    return fake_net_stack().GetInterface(ThreadStackMgrImpl().GetInterfaceName());
  }

  uint64_t GetThreadInterfaceId() {
    const InterfaceInfo* iface = GetThreadInterface();
    if (iface != nullptr) {
      return iface->id;
    } else {
      // ID 0 is sentinel value for an invalid ID.
      return 0;
    }
  }

  const InterfaceInfo* GetTunnelInterface() {
    return fake_net_stack().GetInterface(kTunInterfaceName);
  }

  uint64_t GetTunnelInterfaceId() {
    const InterfaceInfo* iface = GetTunnelInterface();
    if (iface != nullptr) {
      return iface->id;
    } else {
      // ID 0 is sentinel value for an invalid ID.
      return 0;
    }
  }

 private:
  FakeNetStack fake_net_stack_;
  sys::testing::ComponentContextProvider context_provider_;
};

TEST_F(WarmTest, AddRemoveAddressThread) {
  constexpr char kSubnetIp[] = "2001:0DB8:0042::";
  constexpr uint8_t kPrefixLength = 48;
  IPAddress addr;

  // Sanity check - no addresses assigned.
  const InterfaceInfo* iface = GetThreadInterface();
  ASSERT_NE(iface, nullptr);
  EXPECT_EQ(iface->properties.addresses.size(), 0u);

  // Attempt to add the address.
  ASSERT_TRUE(IPAddress::FromString(kSubnetIp, addr));
  auto result = AddRemoveHostAddress(kInterfaceTypeThread, addr, kPrefixLength, /*add*/ true);
  EXPECT_EQ(result, kPlatformResultSuccess);

  // Confirm that it worked.
  iface = GetThreadInterface();
  ASSERT_NE(iface, nullptr);
  ASSERT_EQ(iface->properties.addresses.size(), 1u);
  ASSERT_TRUE(iface->properties.addresses[0].addr.is_ipv6());
  EXPECT_EQ(WeaveIpAddressToArray(addr), iface->properties.addresses[0].addr.ipv6().addr);

  // Attempt to remove the address.
  result = AddRemoveHostAddress(kInterfaceTypeThread, addr, kPrefixLength, /*add*/ false);
  EXPECT_EQ(result, kPlatformResultSuccess);

  // Confirm that it worked.
  iface = GetThreadInterface();
  ASSERT_NE(iface, nullptr);
  EXPECT_EQ(iface->properties.addresses.size(), 0u);
}

TEST_F(WarmTest, AddRemoveAddressTunnel) {
  constexpr char kSubnetIp[] = "2001:0DB8:0042::";
  constexpr uint8_t kPrefixLength = 48;
  IPAddress addr;

  // Sanity check - no addresses assigned.
  const InterfaceInfo* iface = GetTunnelInterface();
  ASSERT_NE(iface, nullptr);
  EXPECT_EQ(iface->properties.addresses.size(), 0u);

  // Attempt to add the address.
  ASSERT_TRUE(IPAddress::FromString(kSubnetIp, addr));
  auto result = AddRemoveHostAddress(kInterfaceTypeTunnel, addr, kPrefixLength, /*add*/ true);
  EXPECT_EQ(result, kPlatformResultSuccess);

  // Confirm that it worked.
  iface = GetTunnelInterface();
  ASSERT_NE(iface, nullptr);
  ASSERT_EQ(iface->properties.addresses.size(), 1u);
  ASSERT_TRUE(iface->properties.addresses[0].addr.is_ipv6());
  EXPECT_EQ(WeaveIpAddressToArray(addr), iface->properties.addresses[0].addr.ipv6().addr);

  // Attempt to remove the address.
  result = AddRemoveHostAddress(kInterfaceTypeTunnel, addr, kPrefixLength, /*add*/ false);
  EXPECT_EQ(result, kPlatformResultSuccess);

  // Confirm that it worked.
  iface = GetTunnelInterface();
  ASSERT_NE(iface, nullptr);
  EXPECT_EQ(iface->properties.addresses.size(), 0u);
}

TEST_F(WarmTest, RemoveAddressThreadNotFound) {
  constexpr char kSubnetIp[] = "2001:0DB8:0042::";
  constexpr uint8_t kPrefixLength = 48;
  IPAddress addr;

  // Sanity check - no addresses assigned.
  const InterfaceInfo* iface = GetThreadInterface();
  ASSERT_NE(iface, nullptr);
  EXPECT_EQ(iface->properties.addresses.size(), 0u);

  // Attempt to remove the address, expecting failure.
  ASSERT_TRUE(IPAddress::FromString(kSubnetIp, addr));
  auto result = AddRemoveHostAddress(kInterfaceTypeThread, addr, kPrefixLength, /*add*/ false);
  EXPECT_EQ(result, kPlatformResultFailure);

  // Sanity check - still no addresses assigned.
  iface = GetThreadInterface();
  ASSERT_NE(iface, nullptr);
  EXPECT_EQ(iface->properties.addresses.size(), 0u);
}

TEST_F(WarmTest, RemoveAddressTunnelNotFound) {
  constexpr char kSubnetIp[] = "2001:0DB8:0042::";
  constexpr uint8_t kPrefixLength = 48;
  IPAddress addr;

  // Sanity check - no addresses assigned.
  const InterfaceInfo* iface = GetTunnelInterface();
  ASSERT_NE(iface, nullptr);
  EXPECT_EQ(iface->properties.addresses.size(), 0u);

  // Attempt to remove the address, expecting failure.
  ASSERT_TRUE(IPAddress::FromString(kSubnetIp, addr));
  auto result = AddRemoveHostAddress(kInterfaceTypeTunnel, addr, kPrefixLength, /*add*/ false);
  EXPECT_EQ(result, kPlatformResultFailure);

  // Sanity check - still no addresses assigned.
  iface = GetTunnelInterface();
  ASSERT_NE(iface, nullptr);
  EXPECT_EQ(iface->properties.addresses.size(), 0u);
}

TEST_F(WarmTest, AddAddressThreadNoInterface) {
  constexpr char kSubnetIp[] = "2001:0DB8:0042::";
  constexpr uint8_t kPrefixLength = 48;
  IPAddress addr;

  fake_net_stack().RemoveFakeInterface(kThreadInterfaceName);

  // Attempt to add to the inteface when there's no Thread interface. Expect failure.
  ASSERT_TRUE(IPAddress::FromString(kSubnetIp, addr));
  auto result = AddRemoveHostAddress(kInterfaceTypeThread, addr, kPrefixLength, /*add*/ true);
  EXPECT_EQ(result, kPlatformResultFailure);
}

TEST_F(WarmTest, RemoveAddressThreadNoInterface) {
  constexpr char kSubnetIp[] = "2001:0DB8:0042::";
  constexpr uint8_t kPrefixLength = 48;
  IPAddress addr;

  fake_net_stack().RemoveFakeInterface(kThreadInterfaceName);

  // Attempt to remove from the inteface when there's no Thread interface. Expect failure.
  ASSERT_TRUE(IPAddress::FromString(kSubnetIp, addr));
  auto result = AddRemoveHostAddress(kInterfaceTypeThread, addr, kPrefixLength, /*add*/ false);
  EXPECT_EQ(result, kPlatformResultFailure);
}

TEST_F(WarmTest, AddAddressTunnelNoInterface) {
  constexpr char kSubnetIp[] = "2001:0DB8:0042::";
  constexpr uint8_t kPrefixLength = 48;
  IPAddress addr;

  fake_net_stack().RemoveFakeInterface(kTunInterfaceName);

  // Attempt to add to the inteface when there's no Tunnel interface. Expect failure.
  ASSERT_TRUE(IPAddress::FromString(kSubnetIp, addr));
  auto result = AddRemoveHostAddress(kInterfaceTypeTunnel, addr, kPrefixLength, /*add*/ true);
  EXPECT_EQ(result, kPlatformResultFailure);
}

TEST_F(WarmTest, RemoveAddressTunnelNoInterface) {
  constexpr char kSubnetIp[] = "2001:0DB8:0042::";
  constexpr uint8_t kPrefixLength = 48;
  IPAddress addr;

  fake_net_stack().RemoveFakeInterface(kTunInterfaceName);

  // Attempt to remove from the inteface when there's no Tunnel interface. Expect failure.
  ASSERT_TRUE(IPAddress::FromString(kSubnetIp, addr));
  auto result = AddRemoveHostAddress(kInterfaceTypeTunnel, addr, kPrefixLength, /*add*/ false);
  EXPECT_EQ(result, kPlatformResultFailure);
}

TEST_F(WarmTest, AddRemoveHostRouteThread) {
  constexpr char kSubnetIp[] = "2001:0DB8:0042::";
  constexpr uint8_t kPrefixLength = 48;
  IPPrefix prefix;

  ASSERT_TRUE(IPAddress::FromString(kSubnetIp, prefix.IPAddr));
  prefix.Length = kPrefixLength;

  // Sanity check - confirm no routes to the Thread interface exist.
  uint64_t thread_iface_id = GetThreadInterfaceId();
  ASSERT_NE(thread_iface_id, 0u);
  const ForwardingEntry* route =
      fake_net_stack().FindForwardingEntry([=](const ForwardingEntry& entry) {
        return entry.destination.is_device_id() && entry.destination.device_id() == thread_iface_id;
      });
  ASSERT_EQ(route, nullptr);

  // Attempt to add a route to the Thread interface.
  auto result = AddRemoveHostRoute(kInterfaceTypeThread, prefix, kRoutePriorityLow, /*add*/ true);
  EXPECT_EQ(result, kPlatformResultSuccess);

  // Confirm that a route exists to the Thread interface with the given IP.
  route = fake_net_stack().FindForwardingEntry([=](const ForwardingEntry& entry) {
    return entry.destination.is_device_id() && entry.destination.device_id() == thread_iface_id;
  });
  ASSERT_NE(route, nullptr);
  ASSERT_TRUE(route->subnet.addr.is_ipv6());
  // Yo dawg, we heard you liked addrs, so we put an addr in your addr.
  EXPECT_EQ(WeaveIpAddressToArray(prefix.IPAddr), route->subnet.addr.ipv6().addr);

  // Remove the route to the Thread interface.
  result = AddRemoveHostRoute(kInterfaceTypeThread, prefix, kRoutePriorityLow, /*add*/ false);
  EXPECT_EQ(result, kPlatformResultSuccess);

  // Confirm that the removal worked.
  route = fake_net_stack().FindForwardingEntry([=](const ForwardingEntry& entry) {
    return entry.destination.is_device_id() && entry.destination.device_id() == thread_iface_id;
  });
  ASSERT_EQ(route, nullptr);
}

TEST_F(WarmTest, AddRemoveHostRouteTunnel) {
  constexpr char kSubnetIp[] = "2001:0DB8:0042::";
  constexpr uint8_t kPrefixLength = 48;
  IPPrefix prefix;

  ASSERT_TRUE(IPAddress::FromString(kSubnetIp, prefix.IPAddr));
  prefix.Length = kPrefixLength;

  // Sanity check - confirm no routes to the Tunnel interface exist.
  uint64_t tunnel_iface_id = GetTunnelInterfaceId();
  ASSERT_NE(tunnel_iface_id, 0u);
  const ForwardingEntry* route =
      fake_net_stack().FindForwardingEntry([=](const ForwardingEntry& entry) {
        return entry.destination.is_device_id() && entry.destination.device_id() == tunnel_iface_id;
      });
  ASSERT_EQ(route, nullptr);

  // Attempt to add a route to the Tunnel interface.
  auto result = AddRemoveHostRoute(kInterfaceTypeTunnel, prefix, kRoutePriorityLow, /*add*/ true);
  EXPECT_EQ(result, kPlatformResultSuccess);

  // Confirm that a route exists to the Tunnel interface with the given IP.
  route = fake_net_stack().FindForwardingEntry([=](const ForwardingEntry& entry) {
    return entry.destination.is_device_id() && entry.destination.device_id() == tunnel_iface_id;
  });
  ASSERT_NE(route, nullptr);
  ASSERT_TRUE(route->subnet.addr.is_ipv6());
  EXPECT_EQ(WeaveIpAddressToArray(prefix.IPAddr), route->subnet.addr.ipv6().addr);

  // Remove the route to the Tunnel interface.
  result = AddRemoveHostRoute(kInterfaceTypeTunnel, prefix, kRoutePriorityLow, /*add*/ false);
  EXPECT_EQ(result, kPlatformResultSuccess);

  // Confirm that the removal worked.
  route = fake_net_stack().FindForwardingEntry([=](const ForwardingEntry& entry) {
    return entry.destination.is_device_id() && entry.destination.device_id() == tunnel_iface_id;
  });
  ASSERT_EQ(route, nullptr);
}

TEST_F(WarmTest, RemoveHostRouteThreadNotFound) {
  constexpr char kSubnetIp[] = "2001:0DB8:0042::";
  constexpr uint8_t kPrefixLength = 48;
  IPPrefix prefix;

  ASSERT_TRUE(IPAddress::FromString(kSubnetIp, prefix.IPAddr));
  prefix.Length = kPrefixLength;

  // Sanity check - confirm no routes to the Thread interface exist.
  uint64_t thread_iface_id = GetThreadInterfaceId();
  ASSERT_NE(thread_iface_id, 0u);
  const ForwardingEntry* route =
      fake_net_stack().FindForwardingEntry([=](const ForwardingEntry& entry) {
        return entry.destination.is_device_id() && entry.destination.device_id() == thread_iface_id;
      });
  ASSERT_EQ(route, nullptr);

  // Remove the non-existent route to the Thread interface, expect failure.
  auto result = AddRemoveHostRoute(kInterfaceTypeThread, prefix, kRoutePriorityLow, /*add*/ false);
  EXPECT_EQ(result, kPlatformResultFailure);

  // Sanity check - confirm still no routes to the Thread interface exist.
  route = fake_net_stack().FindForwardingEntry([=](const ForwardingEntry& entry) {
    return entry.destination.is_device_id() && entry.destination.device_id() == thread_iface_id;
  });
  ASSERT_EQ(route, nullptr);
}

TEST_F(WarmTest, RemoveHostRouteTunnelNotFound) {
  constexpr char kSubnetIp[] = "2001:0DB8:0042::";
  constexpr uint8_t kPrefixLength = 48;
  IPPrefix prefix;

  ASSERT_TRUE(IPAddress::FromString(kSubnetIp, prefix.IPAddr));
  prefix.Length = kPrefixLength;

  // Sanity check - confirm no routes to the Tunnel interface exist.
  uint64_t tunnel_iface_id = GetTunnelInterfaceId();
  ASSERT_NE(tunnel_iface_id, 0u);
  const ForwardingEntry* route =
      fake_net_stack().FindForwardingEntry([=](const ForwardingEntry& entry) {
        return entry.destination.is_device_id() && entry.destination.device_id() == tunnel_iface_id;
      });
  ASSERT_EQ(route, nullptr);

  // Remove the non-existent route to the Tunnel interface, expect failure.
  auto result = AddRemoveHostRoute(kInterfaceTypeTunnel, prefix, kRoutePriorityLow, /*add*/ false);
  EXPECT_EQ(result, kPlatformResultFailure);

  // Sanity check - confirm still no routes to the Tunnel interface exist.
  route = fake_net_stack().FindForwardingEntry([=](const ForwardingEntry& entry) {
    return entry.destination.is_device_id() && entry.destination.device_id() == tunnel_iface_id;
  });
  ASSERT_EQ(route, nullptr);
}

}  // namespace testing
}  // namespace Platform
}  // namespace Warm
}  // namespace Weave
}  // namespace nl
