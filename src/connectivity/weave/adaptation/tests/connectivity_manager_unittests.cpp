// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// clang-format off
#include <Weave/DeviceLayer/internal/WeaveDeviceLayerInternal.h>
#include <Weave/DeviceLayer/ConnectivityManager.h>
#include <Weave/DeviceLayer/internal/ServiceTunnelAgent.h>
#include <Weave/Profiles/WeaveProfiles.h>
#include <Warm/Warm.h>
// clang-format on

#include <deque>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fit/defer.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/syslog/cpp/macros.h>
#include <fuchsia/net/interfaces/cpp/fidl_test_base.h>

#include "src/connectivity/weave/adaptation/configuration_manager_delegate_impl.h"
#include "src/connectivity/weave/adaptation/connectivity_manager_impl.h"
#include "src/connectivity/weave/adaptation/connectivity_manager_delegate_impl.h"
#include "src/connectivity/weave/adaptation/thread_stack_manager_delegate_impl.h"
#include "weave_test_fixture.h"

namespace nl::Weave::DeviceLayer::Internal {
namespace testing {
namespace {

using nl::Weave::DeviceLayer::ConnectivityManager;
using nl::Weave::DeviceLayer::ConnectivityManagerImpl;
using nl::Weave::Profiles::ServiceDirectory::WeaveServiceManager;
using nl::Weave::Profiles::WeaveTunnel::WeaveTunnelAgent;
using nl::Weave::Profiles::WeaveTunnel::WeaveTunnelConnectionMgr;

}  // namespace

class FakeNetInterfaces : public fuchsia::net::interfaces::testing::State_TestBase,
                          public fuchsia::net::interfaces::testing::Watcher_TestBase {
 public:
  FakeNetInterfaces() {
    // Inject an idle event to represent an empty interface list.
    fuchsia::net::interfaces::Empty idle_event;
    fuchsia::net::interfaces::Event event =
        fuchsia::net::interfaces::Event::WithIdle(std::move(idle_event));
    events_.push_back(std::move(event));
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

  void AddEvent(uint64_t id, bool enable_ipv4, bool enable_ipv6, bool enable_intf = false) {
    uint64_t intf_id = id;
    fuchsia::net::interfaces::Event event;
    fuchsia::net::interfaces::Properties properties;
    properties.set_id(intf_id);
    properties.set_has_default_ipv4_route(enable_ipv4);
    properties.set_has_default_ipv6_route(enable_ipv6);
    if (enable_ipv4 || enable_ipv6 || enable_intf) {
      event = fuchsia::net::interfaces::Event::WithChanged(std::move(properties));
    } else {
      event = fuchsia::net::interfaces::Event::WithRemoved(std::move(intf_id));
    }
    events_.push_back(std::move(event));
    SendPendingEvent();
  }

 private:
  async_dispatcher_t* dispatcher_;
  fuchsia::net::interfaces::Watcher::WatchCallback watch_callback_;
  std::deque<fuchsia::net::interfaces::Event> events_;
  fidl::Binding<fuchsia::net::interfaces::State> state_binding_{this};
  fidl::Binding<fuchsia::net::interfaces::Watcher> watcher_binding_{this};
};

class TestWeaveTunnelAgent : public WeaveTunnelAgent {
 public:
  WEAVE_ERROR StartServiceTunnel() override { return WEAVE_NO_ERROR; }

  void StopServiceTunnel(WEAVE_ERROR error) override {}

  bool IsTunnelRoutingRestricted() override { return true; }
};

class TestConnectivityManagerDelegateImpl : public ConnectivityManagerDelegateImpl {
 public:
  WEAVE_ERROR InitServiceTunnelAgent() override {
    // Reset the agent to its default state.
    new (&Internal::ServiceTunnelAgent) TestWeaveTunnelAgent();
    return WEAVE_NO_ERROR;
  }

  bool GetServiceTunnelStarted() { return GetFlag(flags_, kFlag_ServiceTunnelStarted); }

  bool GetServiceTunnelUp() { return GetFlag(flags_, kFlag_ServiceTunnelUp); }
};

class ConnectivityManagerTest : public WeaveTestFixture {
 public:
  ConnectivityManagerTest() {
    context_provider_.service_directory_provider()->AddService(
        fake_net_interfaces_.GetHandler(dispatcher()));
  }

  void SetUp() {
    WeaveTestFixture::SetUp();
    // In order to handle callbacks on the same thread, the delegate cannot be
    // registered while using RunFixtureLoop, which runs the loop in a separate
    // thread context.
    PlatformMgrImpl().SetComponentContextForProcess(context_provider_.TakeContext());
    PlatformMgrImpl().SetDispatcher(dispatcher());
    // Use default ConfigurationManager and mock out tunnel invocation.
    ConfigurationMgrImpl().SetDelegate(std::make_unique<ConfigurationManagerDelegateImpl>());
    ConnectivityMgrImpl().SetDelegate(std::make_unique<TestConnectivityManagerDelegateImpl>());
    ThreadStackMgrImpl().SetDelegate(std::make_unique<ThreadStackManagerDelegateImpl>());
    // Perform initialization of delegate and run to complete FIDL connection.
    EXPECT_EQ(ConnectivityMgrImpl().GetDelegate()->Init(), WEAVE_NO_ERROR);
    RunLoopUntilIdle();
  }

  void TearDown() {
    Warm::Shutdown(FabricState);
    WeaveTestFixture::TearDown();
  }

 protected:
  FakeNetInterfaces fake_net_interfaces_;
  std::vector<WeaveDeviceEvent> application_events_;

  static void HandleApplicationEvent(const WeaveDeviceEvent* event, intptr_t arg) {
    ConnectivityManagerTest* instance = (ConnectivityManagerTest*)arg;
    instance->application_events_.push_back(*event);
  }

  void SetProvisionState(bool provisioned) {
    constexpr uint64_t kFabricId = 1;
    constexpr uint64_t kServiceId = 1;
    constexpr uint8_t kServiceConfig[] = {1};
    constexpr char kAccountId[] = "account-id";
    if (provisioned) {
      EXPECT_EQ(ConfigurationMgr().StoreFabricId(kFabricId), WEAVE_NO_ERROR);
      EXPECT_EQ(
          ConfigurationMgr().StoreServiceProvisioningData(
              kServiceId, kServiceConfig, sizeof(kServiceConfig), kAccountId, sizeof(kAccountId)),
          WEAVE_NO_ERROR);
      EXPECT_TRUE(ConfigurationMgr().IsMemberOfFabric());
    } else {
      EXPECT_EQ(ConfigurationMgr().StoreFabricId(kFabricIdNotSpecified), WEAVE_NO_ERROR);
      EXPECT_EQ(ConfigurationMgr().StoreServiceProvisioningData(0 /* service_id */,
                                                                nullptr, /* service_config */
                                                                0,       /* service_config_len */
                                                                nullptr, /* account_id */
                                                                0 /* account_id_len */),
                WEAVE_NO_ERROR);
    }
  }

  TestConnectivityManagerDelegateImpl* Delegate() {
    return static_cast<TestConnectivityManagerDelegateImpl*>(ConnectivityMgrImpl().GetDelegate());
  }

 private:
  sys::testing::ComponentContextProvider context_provider_;
  std::unique_ptr<sys::ComponentContext> context_;
};

TEST_F(ConnectivityManagerTest, Init) {
  EXPECT_FALSE(ConnectivityMgr().IsServiceTunnelConnected());
  EXPECT_FALSE(ConnectivityMgr().HaveIPv4InternetConnectivity());
  EXPECT_FALSE(ConnectivityMgr().HaveIPv6InternetConnectivity());
  EXPECT_EQ(ConnectivityMgr().GetServiceTunnelMode(),
            ConnectivityManager::kServiceTunnelMode_Enabled);
}

TEST_F(ConnectivityManagerTest, OnInterfaceEvent) {
  constexpr uint64_t kPrimaryIntfId = 1;
  constexpr uint64_t kSecondaryIntfId = kPrimaryIntfId + 1;

  // Report interface with IPv4 connectivity.
  fake_net_interfaces_.AddEvent(kPrimaryIntfId, true, false);
  RunLoopUntilIdle();

  EXPECT_TRUE(ConnectivityMgr().HaveIPv4InternetConnectivity());
  EXPECT_FALSE(ConnectivityMgr().HaveIPv6InternetConnectivity());

  // Report interface with IPv4 and IPv6 connectivity.
  fake_net_interfaces_.AddEvent(kPrimaryIntfId, true, true);
  RunLoopUntilIdle();

  EXPECT_TRUE(ConnectivityMgr().HaveIPv4InternetConnectivity());
  EXPECT_TRUE(ConnectivityMgr().HaveIPv6InternetConnectivity());

  // Report new interface with IPv4 connectivity.
  fake_net_interfaces_.AddEvent(kSecondaryIntfId, true, false);
  RunLoopUntilIdle();

  EXPECT_TRUE(ConnectivityMgr().HaveIPv4InternetConnectivity());
  EXPECT_TRUE(ConnectivityMgr().HaveIPv6InternetConnectivity());

  // Report IPv4 connectivity loss on both interfaces.
  fake_net_interfaces_.AddEvent(kPrimaryIntfId, false, true);
  fake_net_interfaces_.AddEvent(kSecondaryIntfId, false, false);
  RunLoopUntilIdle();

  EXPECT_FALSE(ConnectivityMgr().HaveIPv4InternetConnectivity());
  EXPECT_TRUE(ConnectivityMgr().HaveIPv6InternetConnectivity());

  // Report new interface with no connectivity.
  fake_net_interfaces_.AddEvent(kSecondaryIntfId, false, false, true /* enable_intf */);
  RunLoopUntilIdle();

  EXPECT_FALSE(ConnectivityMgr().HaveIPv4InternetConnectivity());
  EXPECT_TRUE(ConnectivityMgr().HaveIPv6InternetConnectivity());
}

TEST_F(ConnectivityManagerTest, HandleServiceTunnelNotification) {
  PlatformMgr().AddEventHandler(HandleApplicationEvent, (intptr_t)this);
  auto remove_event_handler =
      fit::defer([&] { PlatformMgr().RemoveEventHandler(HandleApplicationEvent, (intptr_t)this); });

  // Enable unrestricted tunnel.
  ServiceTunnelAgent.OnServiceTunStatusNotify(WeaveTunnelConnectionMgr::kStatus_TunPrimaryUp,
                                              WEAVE_NO_ERROR, Delegate());
  RunLoopUntilIdle();
  EXPECT_TRUE(Delegate()->GetServiceTunnelUp());
  EXPECT_EQ(application_events_.size(), 2UL);
  EXPECT_EQ(application_events_[0].Type, DeviceEventType::kServiceTunnelStateChange);
  EXPECT_EQ(application_events_[0].ServiceTunnelStateChange.Result, kConnectivity_Established);
  EXPECT_EQ(application_events_[0].ServiceTunnelStateChange.IsRestricted, false);
  EXPECT_EQ(application_events_[1].Type, DeviceEventType::kServiceConnectivityChange);
  EXPECT_EQ(application_events_[1].ServiceConnectivityChange.ViaTunnel.Result,
            kConnectivity_Established);
  EXPECT_EQ(application_events_[1].ServiceConnectivityChange.ViaThread.Result,
            kConnectivity_NoChange);
  EXPECT_EQ(application_events_[1].ServiceConnectivityChange.Overall.Result,
            kConnectivity_Established);
  application_events_.clear();

  // Bring the tunnel down.
  ServiceTunnelAgent.OnServiceTunStatusNotify(WeaveTunnelConnectionMgr::kStatus_TunDown,
                                              WEAVE_NO_ERROR, Delegate());
  RunLoopUntilIdle();
  EXPECT_FALSE(Delegate()->GetServiceTunnelUp());
  EXPECT_EQ(application_events_.size(), 2UL);
  EXPECT_EQ(application_events_[0].Type, DeviceEventType::kServiceTunnelStateChange);
  EXPECT_EQ(application_events_[0].ServiceTunnelStateChange.Result, kConnectivity_Lost);
  EXPECT_EQ(application_events_[0].ServiceTunnelStateChange.IsRestricted, false);
  EXPECT_EQ(application_events_[1].Type, DeviceEventType::kServiceConnectivityChange);
  EXPECT_EQ(application_events_[1].ServiceConnectivityChange.ViaTunnel.Result, kConnectivity_Lost);
  EXPECT_EQ(application_events_[1].ServiceConnectivityChange.ViaThread.Result,
            kConnectivity_NoChange);
  EXPECT_EQ(application_events_[1].ServiceConnectivityChange.Overall.Result, kConnectivity_Lost);
  application_events_.clear();

  // Enable restricted tunnel.
  ServiceTunnelAgent.OnServiceTunStatusNotify(WeaveTunnelConnectionMgr::kStatus_TunPrimaryUp,
                                              WEAVE_ERROR_TUNNEL_ROUTING_RESTRICTED, Delegate());
  RunLoopUntilIdle();
  EXPECT_TRUE(Delegate()->GetServiceTunnelUp());
  EXPECT_EQ(application_events_.size(), 1UL);
  EXPECT_EQ(application_events_[0].Type, DeviceEventType::kServiceTunnelStateChange);
  EXPECT_EQ(application_events_[0].ServiceTunnelStateChange.Result, kConnectivity_Established);
  EXPECT_EQ(application_events_[0].ServiceTunnelStateChange.IsRestricted, true);
  application_events_.clear();

  // Simulate tunnel down due to error.
  ServiceTunnelAgent.OnServiceTunStatusNotify(WeaveTunnelConnectionMgr::kStatus_TunPrimaryConnError,
                                              WEAVE_ERROR_TIMEOUT, Delegate());
  RunLoopUntilIdle();
  EXPECT_FALSE(Delegate()->GetServiceTunnelUp());
  EXPECT_EQ(application_events_.size(), 2UL);
  EXPECT_EQ(application_events_[0].Type, DeviceEventType::kServiceTunnelStateChange);
  EXPECT_EQ(application_events_[0].ServiceTunnelStateChange.Result, kConnectivity_Lost);
  EXPECT_EQ(application_events_[0].ServiceTunnelStateChange.IsRestricted, false);
  EXPECT_EQ(application_events_[1].Type, DeviceEventType::kServiceConnectivityChange);
  EXPECT_EQ(application_events_[1].ServiceConnectivityChange.ViaTunnel.Result, kConnectivity_Lost);
  EXPECT_EQ(application_events_[1].ServiceConnectivityChange.ViaThread.Result,
            kConnectivity_NoChange);
  EXPECT_EQ(application_events_[1].ServiceConnectivityChange.Overall.Result, kConnectivity_Lost);
  application_events_.clear();
}

TEST_F(ConnectivityManagerTest, OnPlatformEvent) {
  WeaveDeviceEvent fabric_event{.Type = DeviceEventType::kFabricMembershipChange};
  WeaveDeviceEvent provisioning_event{.Type = DeviceEventType::kServiceProvisioningChange};
  WeaveDeviceEvent account_pairing_event{
      .Type = DeviceEventType::kAccountPairingChange,
      .AccountPairingChange.IsPairedToAccount = true,
  };

  // The tunnel should be down by default.
  EXPECT_FALSE(Delegate()->GetServiceTunnelStarted());

  // Enable IPv4 connectivity.
  fake_net_interfaces_.AddEvent(0, true, false);
  RunLoopUntilIdle();

  // Add provisioning information.
  SetProvisionState(true);

  // Send fabric membership change event, which should trigger tunnel start.
  Delegate()->OnPlatformEvent(&fabric_event);
  EXPECT_TRUE(Delegate()->GetServiceTunnelStarted());

  // Remove provisioning information.
  SetProvisionState(false);

  // Send provisioning change event, which should shut the tunnel down.
  Delegate()->OnPlatformEvent(&provisioning_event);
  EXPECT_FALSE(Delegate()->GetServiceTunnelStarted());

  // Setting provision infromation and adding account pairing should restart the
  // tunnel and leave it in the started state.
  SetProvisionState(true);
  Delegate()->OnPlatformEvent(&provisioning_event);
  EXPECT_TRUE(Delegate()->GetServiceTunnelStarted());
  Delegate()->OnPlatformEvent(&account_pairing_event);
  EXPECT_TRUE(Delegate()->GetServiceTunnelStarted());

  // Sending an event when connectivity is down should bring the tunnel down.
  fake_net_interfaces_.AddEvent(0, false, false);
  RunLoopUntilIdle();
  EXPECT_FALSE(Delegate()->GetServiceTunnelStarted());

  // Sending an event that should disable the tunnel should retain its state.
  account_pairing_event.AccountPairingChange.IsPairedToAccount = false;
  Delegate()->OnPlatformEvent(&account_pairing_event);
  EXPECT_FALSE(Delegate()->GetServiceTunnelStarted());
}

}  // namespace testing
}  // namespace nl::Weave::DeviceLayer::Internal
