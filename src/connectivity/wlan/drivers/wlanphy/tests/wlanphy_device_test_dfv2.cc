// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.wlan.wlanphyimpl/cpp/driver/wire.h>
#include <fuchsia/wlan/common/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sync/cpp/completion.h>

#include <zxtest/zxtest.h>

#include "fidl/fuchsia.wlan.wlanphyimpl/cpp/wire_types.h"
#include "src/connectivity/wlan/drivers/wlanphy/device_dfv2.h"
#include "src/connectivity/wlan/drivers/wlanphy/driver.h"
#include "src/devices/testing/mock-ddk/mock-device.h"

namespace wlanphy {
namespace {

// This test class provides the fake upper layer(wlandevicemonitor) and lower layer(wlanphyimpl
// device) for running wlanphy device:
//    |
//    |                              +--------------------+
//    |                             /|  wlandevicemonitor |
//    |                            / +--------------------+
//    |                           /            |
//    |                          /             | <---- [Normal FIDL with protocol:
//    |                         /              |                fuchsia_wlan_device::Phy]
//    |             Both faked           +-----+-----+
//    |            in this test          |  wlanphy  |   <---- Test target
//    |        class(WlanDeviceTest)     |  device   |
//    |                         \        +-----+-----+
//    |                          \             |
//    |                           \            | <---- [Driver transport FIDL with protocol:
//    |                            \           |          fuchsia_wlan_wlanphyimpl::WlanphyImpl]
//    |                             \  +---------------+
//    |                              \ |  wlanphyimpl  |
//    |                                |    device     |
//    |                                +---------------+
//    |
class WlanphyDeviceTest : public ::zxtest::Test,
                          public fdf::WireServer<fuchsia_wlan_wlanphyimpl::WlanphyImpl> {
 public:
  WlanphyDeviceTest()
      : client_loop_phy_(async::Loop(&kAsyncLoopConfigNoAttachToCurrentThread)),
        fake_wlanphy_impl_device_(MockDevice::FakeRootParent()) {
    zx_status_t status = ZX_OK;

    status = wlanphy_init_loop();
    EXPECT_EQ(ZX_OK, status);

    // Create end points for the protocol fuchsia_wlan_device::Phy, these two end points will be
    // bind with WlanphyDeviceTest(as fake wlandevicemonitor) and wlanphy device.
    auto endpoints_phy = fidl::CreateEndpoints<fuchsia_wlan_device::Phy>();
    ASSERT_FALSE(endpoints_phy.is_error());

    // Create end points for the protocol fuchsia_wlan_wlanphyimpl::WlanphyImpl, these two end
    // points will be bind with wlanphy device and WlanphyDeviceTest(as fake WlanphyImplDevice).
    auto endpoints_phy_impl = fdf::CreateEndpoints<fuchsia_wlan_wlanphyimpl::WlanphyImpl>();
    ASSERT_FALSE(endpoints_phy_impl.is_error());

    status = client_loop_phy_.StartThread("fake-wlandevicemonitor-loop");
    // There is a return value in ASSERT_EQ(), cannot apply here.
    EXPECT_EQ(ZX_OK, status);

    // Cache the client dispatcher of fuchsia_wlan_device::Phy protocol.
    client_dispatcher_phy_ = client_loop_phy_.dispatcher();

    // Create the client for the protocol fuchsia_wlan_device::Phy based on dispatcher end
    // point.
    client_phy_ = fidl::WireSharedClient<fuchsia_wlan_device::Phy>(std::move(endpoints_phy->client),
                                                                   client_dispatcher_phy_);

    // Create the server dispatcher of fuchsia_wlan_wlanphyimpl::WlanphyImpl protocol, its lifecycle
    // will be maintained by this test class.
    auto server_dispatcher_phy_impl_status = fdf::Dispatcher::Create(
        0, "wlan-phy-test",
        [&](fdf_dispatcher_t*) { server_dispatcher_phy_impl_completion_.Signal(); });
    ASSERT_FALSE(server_dispatcher_phy_impl_status.is_error());
    server_dispatcher_phy_impl_ = std::move(*server_dispatcher_phy_impl_status);

    // Establish FIDL connection based on fuchsia_wlan_wlanphyimpl::WlanphyImpl protocol.
    wlanphy_device_ =
        new Device(fake_wlanphy_impl_device_.get(), std::move(endpoints_phy_impl->client));
    EXPECT_EQ(ZX_OK, wlanphy_device_->DeviceAdd());
    // Call DdkAsyncRemove() here to make ReleaseFlaggedDevices fully executed. This function will
    // stop the device from working properly, so we can call it anywhere before
    // ReleaseFlaggedDevices() is called. We just call it as soon as the device is created here.
    wlanphy_device_->DdkAsyncRemove();

    fdf::BindServer<fdf::WireServer<fuchsia_wlan_wlanphyimpl::WlanphyImpl>>(
        server_dispatcher_phy_impl_.get(), std::move(endpoints_phy_impl->server), this);

    // Establish FIDL connection based on fuchsia_wlan_device::Phy protocol.
    wlanphy_device_->Connect(std::move(endpoints_phy->server));

    // Initialize struct to avoid random values.
    memset(static_cast<void*>(&create_iface_req_), 0, sizeof(create_iface_req_));
  }

  ~WlanphyDeviceTest() {
    // Mock DDK will delete wlanphy_device_.
    mock_ddk::ReleaseFlaggedDevices(wlanphy_device_->zxdev());
    server_dispatcher_phy_impl_.ShutdownAsync();
    server_dispatcher_phy_impl_completion_.Wait();
  }

  // Server end handler functions for fuchsia_wlan_wlanphyimpl::WlanphyImpl.
  void GetSupportedMacRoles(fdf::Arena& arena,
                            GetSupportedMacRolesCompleter::Sync& completer) override {
    std::vector<fuchsia_wlan_common::wire::WlanMacRole> supported_mac_roles_vec;
    supported_mac_roles_vec.push_back(kFakeMacRole);
    auto supported_mac_roles =
        fidl::VectorView<fuchsia_wlan_common::wire::WlanMacRole>::FromExternal(
            supported_mac_roles_vec);
    completer.buffer(arena).ReplySuccess(supported_mac_roles);
    test_completion_.Signal();
  }
  void CreateIface(CreateIfaceRequestView request, fdf::Arena& arena,
                   CreateIfaceCompleter::Sync& completer) override {
    has_init_sta_addr_ = false;
    if (request->has_init_sta_addr()) {
      create_iface_req_.init_sta_addr = request->init_sta_addr();
      has_init_sta_addr_ = true;
    }
    if (request->has_role()) {
      create_iface_req_.role = request->role();
    }

    fidl::Arena fidl_arena;
    auto builder =
        fuchsia_wlan_wlanphyimpl::wire::WlanphyImplCreateIfaceResponse::Builder(fidl_arena);
    builder.iface_id(kFakeIfaceId);
    completer.buffer(arena).ReplySuccess(builder.Build());
    test_completion_.Signal();
  }
  void DestroyIface(DestroyIfaceRequestView request, fdf::Arena& arena,
                    DestroyIfaceCompleter::Sync& completer) override {
    destroy_iface_id_ = request->iface_id();
    completer.buffer(arena).ReplySuccess();
    test_completion_.Signal();
  }
  void SetCountry(SetCountryRequestView request, fdf::Arena& arena,
                  SetCountryCompleter::Sync& completer) override {
    country_ = request->country;
    completer.buffer(arena).ReplySuccess();
    test_completion_.Signal();
  }
  void ClearCountry(fdf::Arena& arena, ClearCountryCompleter::Sync& completer) override {
    completer.buffer(arena).ReplySuccess();
    test_completion_.Signal();
  }
  void GetCountry(fdf::Arena& arena, GetCountryCompleter::Sync& completer) override {
    auto country = fuchsia_wlan_wlanphyimpl::wire::WlanphyCountry::WithAlpha2(kAlpha2);
    completer.buffer(arena).ReplySuccess(country);
    test_completion_.Signal();
  }
  void SetPsMode(SetPsModeRequestView request, fdf::Arena& arena,
                 SetPsModeCompleter::Sync& completer) override {
    ps_mode_ = request->ps_mode();
    completer.buffer(arena).ReplySuccess();
    test_completion_.Signal();
  }
  void GetPsMode(fdf::Arena& arena, GetPsModeCompleter::Sync& completer) override {
    fidl::Arena fidl_arena;
    auto builder =
        fuchsia_wlan_wlanphyimpl::wire::WlanphyImplGetPsModeResponse::Builder(fidl_arena);
    builder.ps_mode(kFakePsMode);

    completer.buffer(arena).ReplySuccess(builder.Build());
    test_completion_.Signal();
  }

  // Record the create iface request data when fake wlanphyimpl device gets it.
  fuchsia_wlan_device::wire::CreateIfaceRequest create_iface_req_;
  bool has_init_sta_addr_;

  // Record the destroy iface request data when fake wlanphyimpl device gets it.
  uint16_t destroy_iface_id_;

  // Record the country data when fake wlanphyimpl device gets it.
  fuchsia_wlan_wlanphyimpl::wire::WlanphyCountry country_;

  // Record the power save mode data when fake wlanphyimpl device gets it.
  fuchsia_wlan_common::wire::PowerSaveType ps_mode_;

  static constexpr fuchsia_wlan_common::wire::WlanMacRole kFakeMacRole =
      fuchsia_wlan_common::wire::WlanMacRole::kAp;
  static constexpr uint16_t kFakeIfaceId = 1;
  static constexpr fidl::Array<uint8_t, WLANPHY_ALPHA2_LEN> kAlpha2{'W', 'W'};
  static constexpr fuchsia_wlan_common::wire::PowerSaveType kFakePsMode =
      fuchsia_wlan_common::wire::PowerSaveType::kPsModePerformance;
  static constexpr ::fidl::Array<uint8_t, 6> kValidStaAddr = {1, 2, 3, 4, 5, 6};
  static constexpr ::fidl::Array<uint8_t, 6> kInvalidStaAddr = {0, 0, 0, 0, 0, 0};

  // The completion to synchronize the state in tests, because there are async FIDL calls.
  libsync::Completion test_completion_;

 protected:
  // The FIDL client to communicate with wlanphy device.
  fidl::WireSharedClient<fuchsia_wlan_device::Phy> client_phy_;

  void* dummy_ctx_;

 private:
  async::Loop client_loop_phy_;

  // Dispatcher for the FIDL client sending requests to wlanphy device.
  async_dispatcher_t* client_dispatcher_phy_;

  // Dispatcher for being a driver transport FIDL server to receive and dispatch requests from
  // wlanphy device.
  fdf::Dispatcher server_dispatcher_phy_impl_;

  // fake zx_device as the the parent of wlanphy device.
  std::shared_ptr<MockDevice> fake_wlanphy_impl_device_;
  Device* wlanphy_device_;

  // The completion to ensure server_dispatcher_phy_impl_'s shutdown before destroy.
  libsync::Completion server_dispatcher_phy_impl_completion_;
};

TEST_F(WlanphyDeviceTest, GetSupportedMacRoles) {
  auto result = client_phy_.sync()->GetSupportedMacRoles();
  ASSERT_TRUE(result.ok());
  test_completion_.Wait();

  EXPECT_EQ(1U, result->value()->supported_mac_roles.count());
  EXPECT_EQ(kFakeMacRole, result->value()->supported_mac_roles.data()[0]);
}

TEST_F(WlanphyDeviceTest, CreateIfaceTestNullAddr) {
  auto dummy_ends = fidl::CreateEndpoints<fuchsia_wlan_device::Phy>();
  auto dummy_channel = dummy_ends->server.TakeChannel();
  // All-zero MAC address in the request will should result in a false on has_init_sta_addr in
  // next level's FIDL request.
  fuchsia_wlan_device::wire::CreateIfaceRequest req = {
      .role = fuchsia_wlan_common::wire::WlanMacRole::kClient,
      .mlme_channel = std::move(dummy_channel),
      .init_sta_addr = kInvalidStaAddr,
  };

  auto result = client_phy_.sync()->CreateIface(std::move(req));
  ASSERT_TRUE(result.ok());
  test_completion_.Wait();
  EXPECT_FALSE(has_init_sta_addr_);
}

TEST_F(WlanphyDeviceTest, CreateIfaceTestValidAddr) {
  auto dummy_ends = fidl::CreateEndpoints<fuchsia_wlan_device::Phy>();
  auto dummy_channel = dummy_ends->server.TakeChannel();

  fuchsia_wlan_device::wire::CreateIfaceRequest req = {
      .role = fuchsia_wlan_common::wire::WlanMacRole::kClient,
      .mlme_channel = std::move(dummy_channel),
      .init_sta_addr = kValidStaAddr,
  };

  auto result = client_phy_.sync()->CreateIface(std::move(req));
  ASSERT_TRUE(result.ok());
  test_completion_.Wait();
  EXPECT_TRUE(has_init_sta_addr_);
  EXPECT_EQ(0, memcmp(&create_iface_req_.init_sta_addr, &req.init_sta_addr.data()[0],
                      fuchsia_wlan_ieee80211::wire::kMacAddrLen));
  EXPECT_EQ(kFakeIfaceId, result->value()->iface_id);
}

TEST_F(WlanphyDeviceTest, DestroyIface) {
  fuchsia_wlan_device::wire::DestroyIfaceRequest req = {
      .id = kFakeIfaceId,
  };

  auto result = client_phy_.sync()->DestroyIface(std::move(req));
  ASSERT_TRUE(result.ok());
  test_completion_.Wait();
  EXPECT_EQ(req.id, destroy_iface_id_);
}

TEST_F(WlanphyDeviceTest, SetCountry) {
  fuchsia_wlan_device::wire::CountryCode country_code = {
      .alpha2 =
          {
              .data_ = {'U', 'S'},
          },
  };
  auto result = client_phy_.sync()->SetCountry(std::move(country_code));
  ASSERT_TRUE(result.ok());
  test_completion_.Wait();

  EXPECT_EQ(0, memcmp(&country_code.alpha2.data()[0], &country_.alpha2().data()[0],
                      fuchsia_wlan_wlanphyimpl::wire::kWlanphyAlpha2Len));
}

TEST_F(WlanphyDeviceTest, GetCountry) {
  auto result = client_phy_.sync()->GetCountry();
  ASSERT_TRUE(result.ok());
  test_completion_.Wait();

  EXPECT_EQ(0, memcmp(&result->value()->resp.alpha2.data()[0], &kAlpha2.data()[0],
                      fuchsia_wlan_wlanphyimpl::wire::kWlanphyAlpha2Len));
}

TEST_F(WlanphyDeviceTest, ClearCountry) {
  auto result = client_phy_.sync()->ClearCountry();
  ASSERT_TRUE(result.ok());
  test_completion_.Wait();
}

TEST_F(WlanphyDeviceTest, SetPsMode) {
  fuchsia_wlan_common::wire::PowerSaveType ps_mode =
      fuchsia_wlan_common::wire::PowerSaveType::kPsModeLowPower;
  auto result = client_phy_.sync()->SetPsMode(std::move(ps_mode));
  ASSERT_TRUE(result.ok());
  test_completion_.Wait();

  EXPECT_EQ(ps_mode_, ps_mode);
}

TEST_F(WlanphyDeviceTest, GetPsMode) {
  auto result = client_phy_.sync()->GetPsMode();
  ASSERT_TRUE(result.ok());
  test_completion_.Wait();

  EXPECT_EQ(kFakePsMode, result->value()->resp);
}
}  // namespace
}  // namespace wlanphy
