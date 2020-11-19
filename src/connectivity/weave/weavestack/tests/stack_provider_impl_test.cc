// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "src/connectivity/weave/weavestack/fidl/stack_provider_impl.h"

#include <fuchsia/weave/cpp/fidl_test_base.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/syslog/cpp/macros.h>

// clang-format off
#include <Weave/DeviceLayer/internal/WeaveDeviceLayerInternal.h>
#include <Weave/DeviceLayer/PlatformManager.h>

#include "generic_platform_manager_impl_fuchsia.ipp"
#include "configuration_manager_delegate_impl.h"
#include "connectivity_manager_delegate_impl.h"
#include "network_provisioning_server_delegate_impl.h"
#include "thread_stack_manager_delegate_impl.h"
// clang-format on

#include <gtest/gtest.h>

namespace weavestack {
namespace {
using nl::Weave::DeviceLayer::ConfigurationManagerDelegateImpl;
using nl::Weave::DeviceLayer::ConfigurationMgrImpl;
using nl::Weave::DeviceLayer::ConnectivityManagerDelegateImpl;
using nl::Weave::DeviceLayer::ConnectivityMgrImpl;
using nl::Weave::DeviceLayer::PlatformMgrImpl;
using nl::Weave::DeviceLayer::ThreadStackManagerDelegateImpl;
using nl::Weave::DeviceLayer::ThreadStackMgrImpl;
using nl::Weave::DeviceLayer::Internal::DeviceNetworkInfo;
using nl::Weave::DeviceLayer::Internal::NetworkProvisioningServerDelegateImpl;
using nl::Weave::DeviceLayer::Internal::NetworkProvisioningServerImpl;
using nl::Weave::DeviceLayer::Internal::NetworkProvisioningSvrImpl;

}  // namespace

class FakeWlanNetworkConfigProvider
    : public fuchsia::weave::testing::WlanNetworkConfigProvider_TestBase {
 public:
  FakeWlanNetworkConfigProvider() : binding_(this) {}
  void NotImplemented_(const std::string &name) override { FAIL() << __func__; }

  void WatchConnectedNetwork(WatchConnectedNetworkCallback callback) override {
    wlan_update_callback_ = std::move(callback);
  }

  void ReportWlanUpdate(fuchsia::wlan::policy::NetworkConfig network_config) {
    wlan_update_callback_(std::move(network_config));
  }

  fidl::InterfaceHandle<fuchsia::weave::WlanNetworkConfigProvider> &GetInterfaceHandle() {
    binding_.Bind(interface_handle_.NewRequest());
    return interface_handle_;
  }

  bool IsInterfaceBound() { return binding_.is_bound(); }

 private:
  fidl::Binding<fuchsia::weave::WlanNetworkConfigProvider> binding_;
  fidl::InterfaceHandle<fuchsia::weave::WlanNetworkConfigProvider> interface_handle_;
  WatchConnectedNetworkCallback wlan_update_callback_;
};

class StackProviderImplTest : public gtest::TestLoopFixture {
 public:
  void SetUp() override {
    TestLoopFixture::SetUp();

    // Initialize the weave stack
    PlatformMgrImpl().SetDispatcher(dispatcher());
    ConfigurationMgrImpl().SetDelegate(std::make_unique<ConfigurationManagerDelegateImpl>());
    ConnectivityMgrImpl().SetDelegate(std::make_unique<ConnectivityManagerDelegateImpl>());
    ThreadStackMgrImpl().SetDelegate(std::make_unique<ThreadStackManagerDelegateImpl>());
    NetworkProvisioningSvrImpl().SetDelegate(
        std::make_unique<NetworkProvisioningServerDelegateImpl>());
    ASSERT_EQ(PlatformMgrImpl().InitWeaveStack(), WEAVE_NO_ERROR);

    // Set up StackImpl
    stack_provider_impl_ = std::make_unique<StackProviderImpl>(provider_.context());
    stack_provider_impl_->Init();
    ASSERT_FALSE(weave_stack_provider_.is_bound());

    // Connect to the UUT
    provider_.ConnectToPublicService(weave_stack_provider_.NewRequest());
    ASSERT_TRUE(weave_stack_provider_.is_bound());
  }

  void TearDown() override {
    // Shut down the weave stack
    PlatformMgrImpl().ShutdownWeaveStack();
    TestLoopFixture::TearDown();
  }

 protected:
  fuchsia::weave::StackProviderPtr weave_stack_provider_;
  std::unique_ptr<StackProviderImpl> stack_provider_impl_;

 private:
  sys::testing::ComponentContextProvider provider_;
};

// Test Cases ------------------------------------------------------------------

TEST_F(StackProviderImplTest, GetWiFiStationProvision) {
  DeviceNetworkInfo netInfo;
  FakeWlanNetworkConfigProvider fake_wlan_provider;

  EXPECT_EQ(NetworkProvisioningSvrImpl().GetDelegate()->GetWiFiStationProvision(netInfo, true),
            WEAVE_ERROR_INCORRECT_STATE);

  weave_stack_provider_->SetWlanNetworkConfigProvider(
      std::move(fake_wlan_provider.GetInterfaceHandle()));
  // Run loop until NetworkProvisioningSvr binds with |fake_wlan_provider|
  RunLoopUntilIdle();
  {
    constexpr char test_ssid[] = "TESTSSID";
    constexpr char test_password[] = "TESTPASSWORD";
    fuchsia::wlan::policy::NetworkIdentifier network_id;
    fuchsia::wlan::policy::NetworkConfig network_config;
    network_id.ssid.assign(std::begin(test_ssid), std::end(test_ssid));
    network_id.type = fuchsia::wlan::policy::SecurityType::WPA2;
    network_config.set_id(network_id);
    network_config.set_credential(fuchsia::wlan::policy::Credential::WithPassword(
        std::vector<uint8_t>(std::begin(test_password), std::end(test_password))));

    fake_wlan_provider.ReportWlanUpdate(std::move(network_config));
    // Run loop until NetworkProvisioningSvr process WLAN update
    RunLoopUntilIdle();
    EXPECT_EQ(NetworkProvisioningSvrImpl().GetDelegate()->GetWiFiStationProvision(netInfo, true),
              WEAVE_NO_ERROR);
    EXPECT_STREQ((const char *)netInfo.WiFiSSID, test_ssid);
    EXPECT_EQ(netInfo.WiFiSecurityType,
              nl::Weave::Profiles::NetworkProvisioning::kWiFiSecurityType_WPA2Personal);
    EXPECT_EQ(netInfo.WiFiKeyLen, sizeof(test_password));
    EXPECT_STREQ((const char *)netInfo.WiFiKey, test_password);

    netInfo.Reset();
    EXPECT_EQ(NetworkProvisioningSvrImpl().GetDelegate()->GetWiFiStationProvision(netInfo, false),
              WEAVE_NO_ERROR);
    EXPECT_STREQ((const char *)netInfo.WiFiSSID, test_ssid);
    EXPECT_EQ(netInfo.WiFiKeyLen, 0);
  }

  {
    constexpr char test_ssid[] = "TESTSSID2";
    fuchsia::wlan::policy::NetworkIdentifier network_id;
    fuchsia::wlan::policy::NetworkConfig network_config;
    network_id.ssid.assign(std::begin(test_ssid), std::end(test_ssid));
    network_id.type = fuchsia::wlan::policy::SecurityType::NONE;
    network_config.set_id(network_id);
    network_config.set_credential(
        fuchsia::wlan::policy::Credential::WithNone(fuchsia::wlan::policy::Empty()));

    fake_wlan_provider.ReportWlanUpdate(std::move(network_config));
    // Run loop until NetworkProvisioningSvr process WLAN update
    RunLoopUntilIdle();
    EXPECT_EQ(NetworkProvisioningSvrImpl().GetDelegate()->GetWiFiStationProvision(netInfo, true),
              WEAVE_NO_ERROR);
    EXPECT_STREQ((const char *)netInfo.WiFiSSID, test_ssid);
    EXPECT_EQ(netInfo.WiFiSecurityType,
              nl::Weave::Profiles::NetworkProvisioning::kWiFiSecurityType_None);
  }
}

TEST_F(StackProviderImplTest, SetWlanNetworkConfigProvider) {
  FakeWlanNetworkConfigProvider fake_wlan_provider;
  FakeWlanNetworkConfigProvider fake_wlan_provider_new;

  EXPECT_FALSE(fake_wlan_provider.IsInterfaceBound());
  EXPECT_FALSE(fake_wlan_provider_new.IsInterfaceBound());
  weave_stack_provider_->SetWlanNetworkConfigProvider(
      std::move(fake_wlan_provider.GetInterfaceHandle()));
  // Run loop until NetworkProvisioningSvr binds with |fake_wlan_provider|
  RunLoopUntilIdle();
  EXPECT_TRUE(fake_wlan_provider.IsInterfaceBound());
  weave_stack_provider_->SetWlanNetworkConfigProvider(
      std::move(fake_wlan_provider_new.GetInterfaceHandle()));
  // Run loop until NetworkProvisioningSvr binds with |fake_wlan_provider_new|
  RunLoopUntilIdle();
  EXPECT_FALSE(fake_wlan_provider.IsInterfaceBound());
  EXPECT_TRUE(fake_wlan_provider_new.IsInterfaceBound());
}

}  // namespace weavestack
