// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// clang-format off
#include <Weave/DeviceLayer/internal/WeaveDeviceLayerInternal.h>
#include <Weave/DeviceLayer/ConfigurationManager.h>
#include <Weave/Core/WeaveKeyIds.h>
#include "configuration_manager_impl.h"
#include "fuchsia_config.h"
#include "group_key_store_impl.h"
#include <Weave/Profiles/security/WeaveApplicationKeys.h>

#include <Weave/Core/WeaveVendorIdentifiers.hpp>
#include <Weave/DeviceLayer/internal/GenericConfigurationManagerImpl.ipp>
// clang-format on

#include <fuchsia/wlan/device/service/cpp/fidl.h>
#include <fuchsia/wlan/device/service/cpp/fidl_test_base.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <net/ethernet.h>

#include "configuration_manager_impl.h"
#include "gtest/gtest.h"

namespace adaptation {
namespace testing {
namespace {

using nl::Weave::DeviceLayer::ConfigurationMgr;
using nl::Weave::DeviceLayer::ConfigurationMgrImpl;

constexpr uint8_t kExpectedMac[] = {124, 46, 119, 21, 27, 102};
constexpr uint16_t kExpectedPhyId = 0;
constexpr char kExpectedPath[] = "/dev/wifi/wlanphy";

}  // namespace

class FakeWlanStack : public fuchsia::wlan::device::service::testing::DeviceService_TestBase {
 public:
  void NotImplemented_(const std::string& name) override { FAIL() << __func__; }

  void ListPhys(ListPhysCallback callback) override {
    fuchsia::wlan::device::service::ListPhysResponse resp;
    fuchsia::wlan::device::service::PhyListItem item;
    item.phy_id = kExpectedPhyId;
    item.path = kExpectedPath;
    resp.phys.push_back(item);
    callback(resp);
  }

  void QueryPhy(fuchsia::wlan::device::service::QueryPhyRequest req,
                QueryPhyCallback callback) override {
    std::unique_ptr<fuchsia::wlan::device::service::QueryPhyResponse> resp =
        std::make_unique<fuchsia::wlan::device::service::QueryPhyResponse>();
    resp->info.id = 0;
    std::copy(std::begin(kExpectedMac), std::end(kExpectedMac), resp->info.hw_mac_address.begin());
    resp->info.mac_roles = {fuchsia::wlan::device::MacRole::CLIENT,
                            fuchsia::wlan::device::MacRole::AP};
    callback(0, std::move(resp));
  }

  fidl::InterfaceRequestHandler<fuchsia::wlan::device::service::DeviceService> GetHandler(
      async_dispatcher_t* dispatcher = nullptr) {
    dispatcher_ = dispatcher;
    return [this, dispatcher](
               fidl::InterfaceRequest<fuchsia::wlan::device::service::DeviceService> request) {
      binding_.Bind(std::move(request), dispatcher);
    };
  }

 private:
  fidl::Binding<fuchsia::wlan::device::service::DeviceService> binding_{this};
  async_dispatcher_t* dispatcher_;
};

class ConfigurationManagerTest : public ::gtest::TestLoopFixture {
 public:
  ConfigurationManagerTest() : context_provider_(), cfg_mgr_(context_provider_.TakeContext()) {
    context_provider_.service_directory_provider()->AddService(
        fake_wlan_stack_.GetHandler(loop_.dispatcher()));
    loop_.StartThread();
  }
  void SetUp() override { TestLoopFixture::SetUp(); }

 private:
  async::Loop loop_ = async::Loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  sys::testing::ComponentContextProvider context_provider_;
  FakeWlanStack fake_wlan_stack_;

 public:
  nl::Weave::DeviceLayer::ConfigurationManagerImpl cfg_mgr_;
};

TEST_F(ConfigurationManagerTest, SetAndGetFabricId) {
  const uint64_t fabric_id = 123456789U;
  uint64_t stored_fabric_id = 0U;
  RunLoopUntilIdle();
  EXPECT_EQ(cfg_mgr_.StoreFabricId(fabric_id), WEAVE_NO_ERROR);
  EXPECT_EQ(cfg_mgr_.GetFabricId(stored_fabric_id), WEAVE_NO_ERROR);
  EXPECT_EQ(stored_fabric_id, fabric_id);
}
TEST_F(ConfigurationManagerTest, GetPrimaryWiFiMacAddress) {
  uint8_t mac[ETH_ALEN];
  RunLoopUntilIdle();
  EXPECT_EQ(cfg_mgr_.GetPrimaryWiFiMACAddress(mac), WEAVE_NO_ERROR);
  EXPECT_TRUE(std::equal(std::begin(kExpectedMac), std::end(kExpectedMac), std::begin(mac)));
}

}  // namespace testing
}  // namespace adaptation
