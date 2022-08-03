// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/wlan/common/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>

#include <gtest/gtest.h>

#include "src/connectivity/wlan/drivers/wlanphy/device.h"
#include "src/connectivity/wlan/drivers/wlanphy/driver.h"

namespace wlanphy {
namespace {

class WlanphyDeviceTest : public ::testing::Test {
 public:
  WlanphyDeviceTest() : client_loop_(async::Loop(&kAsyncLoopConfigNoAttachToCurrentThread)) {
    zx_status_t status = ZX_OK;

    status = client_loop_.StartThread("fake-wlandevicemonitor-loop");
    // There is a return value in ASSERT_EQ(), cannot apply here.
    EXPECT_EQ(ZX_OK, status);

    client_dispatcher_ = client_loop_.dispatcher();

    void* dummy_ctx_ = nullptr;
    wlanphy_init(&dummy_ctx_);
    fake_wlanphy_impl_protocol_.ctx = dummy_ctx_;
  }

  void ConnectToWlanphyDevice() {
    auto endpoints = fidl::CreateEndpoints<fuchsia_wlan_device::Phy>();
    ASSERT_FALSE(endpoints.is_error());

    client_ = fidl::WireSharedClient<fuchsia_wlan_device::Phy>(std::move(endpoints->client),
                                                               client_dispatcher_);

    wlanphy_device_ = std::make_unique<Device>(nullptr, fake_wlanphy_impl_protocol_);
    wlanphy_device_->Connect(std::move(endpoints->server));
  }

  wlanphy_impl_protocol_ops_t fake_wlanphy_impl_protocol_ops_ = {
      .get_supported_mac_roles =
          [](void* ctx,
             wlan_mac_role_t supported_mac_roles_list[fuchsia_wlan_common_MAX_SUPPORTED_MAC_ROLES],
             uint8_t* supported_mac_roles_count) -> zx_status_t { return ZX_ERR_NOT_SUPPORTED; },
      .create_iface = [](void* ctx, const wlanphy_impl_create_iface_req_t* req,
                         uint16_t* out_iface_id) -> zx_status_t { return ZX_ERR_NOT_SUPPORTED; },
      .destroy_iface = [](void* ctx, uint16_t id) -> zx_status_t { return ZX_ERR_NOT_SUPPORTED; },
      .set_country = [](void* ctx, const wlanphy_country_t* country) -> zx_status_t {
        return ZX_ERR_NOT_SUPPORTED;
      },
      .clear_country = [](void* ctx) -> zx_status_t { return ZX_ERR_NOT_SUPPORTED; },
      .get_country = [](void* ctx, wlanphy_country_t* country) -> zx_status_t {
        return ZX_ERR_NOT_SUPPORTED;
      },
      .set_ps_mode = [](void* ctx, const wlanphy_ps_mode_t* ps_mode) -> zx_status_t {
        return ZX_ERR_NOT_SUPPORTED;
      },
      .get_ps_mode = [](void* ctx, wlanphy_ps_mode_t* ps_mode) -> zx_status_t {
        return ZX_ERR_NOT_SUPPORTED;
      },
  };

  wlanphy_impl_protocol_t fake_wlanphy_impl_protocol_ = {
      .ops = &fake_wlanphy_impl_protocol_ops_,
  };

 protected:
  // The FIDL client to communicate with wlanphy device.
  fidl::WireSharedClient<fuchsia_wlan_device::Phy> client_;

  void* dummy_ctx_;

 private:
  async::Loop client_loop_;

  // Dispatcher for the FIDL client sending requests to wlanphy device.
  async_dispatcher_t* client_dispatcher_;

  std::unique_ptr<Device> wlanphy_device_;
};

static constexpr uint16_t kFakeIfaceId = 3;
static constexpr uint8_t kFakeMacAddr[fuchsia_wlan_ieee80211::wire::kMacAddrLen] = {2, 2, 3,
                                                                                    3, 4, 5};
TEST_F(WlanphyDeviceTest, GetSupportedMacRolesTest) {
  ConnectToWlanphyDevice();
  fake_wlanphy_impl_protocol_ops_.get_supported_mac_roles =
      [](void* ctx,
         wlan_mac_role_t supported_mac_roles_list[fuchsia_wlan_common_MAX_SUPPORTED_MAC_ROLES],
         uint8_t* supported_mac_roles_count) -> zx_status_t {
    supported_mac_roles_list[0] = WLAN_MAC_ROLE_AP;
    supported_mac_roles_list[1] = WLAN_MAC_ROLE_MESH;
    *supported_mac_roles_count = 2;
    return ZX_OK;
  };

  auto result = client_.sync()->GetSupportedMacRoles();
  ASSERT_TRUE(result.ok());

  EXPECT_EQ(2U, result->value()->supported_mac_roles.count());
  EXPECT_EQ(fuchsia_wlan_common::wire::WlanMacRole::kAp,
            result->value()->supported_mac_roles.data()[0]);
  EXPECT_EQ(fuchsia_wlan_common::wire::WlanMacRole::kMesh,
            result->value()->supported_mac_roles.data()[1]);
}

TEST_F(WlanphyDeviceTest, CreateIfaceRequestConvertTest) {
  ConnectToWlanphyDevice();

  auto dummy_ends = fidl::CreateEndpoints<fuchsia_wlan_device::Phy>();
  auto dummy_channel = dummy_ends->server.TakeChannel();
  {
    fuchsia_wlan_device::wire::CreateIfaceRequest req_in = {
        .role = fuchsia_wlan_common::wire::WlanMacRole::kAp,
        .mlme_channel = std::move(dummy_channel),
        .init_sta_addr =
            {
                .data_ = {kFakeMacAddr[0], kFakeMacAddr[1], kFakeMacAddr[2], kFakeMacAddr[3],
                          kFakeMacAddr[4], kFakeMacAddr[5]},
            },
    };

    fake_wlanphy_impl_protocol_ops_.create_iface = [](void* ctx,
                                                       const wlanphy_impl_create_iface_req_t* req,
                                                       uint16_t* out_iface_id) -> zx_status_t {
      EXPECT_EQ(WLAN_MAC_ROLE_AP, req->role);
      EXPECT_TRUE(req->has_init_sta_addr);
      EXPECT_EQ(0, memcmp(&kFakeMacAddr[0], &req->init_sta_addr[0],
                          fuchsia_wlan_ieee80211::wire::kMacAddrLen));
      *out_iface_id = kFakeIfaceId;
      return ZX_OK;
    };

    auto result = client_.sync()->CreateIface(std::move(req_in));
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(kFakeIfaceId, result->value()->iface_id);
  }

  // When the init_sta_addr in the request is all-zero, it means no MAC address is set.
  {
    fuchsia_wlan_device::wire::CreateIfaceRequest req_in = {
        .role = fuchsia_wlan_common::wire::WlanMacRole::kAp,
        .mlme_channel = std::move(dummy_channel),
        .init_sta_addr =
            {
                .data_ = {0, 0, 0, 0, 0, 0},
            },
    };

    fake_wlanphy_impl_protocol_ops_.create_iface = [](void* ctx,
                                                       const wlanphy_impl_create_iface_req_t* req,
                                                       uint16_t* out_iface_id) -> zx_status_t {
      EXPECT_EQ(WLAN_MAC_ROLE_AP, req->role);
      EXPECT_FALSE(req->has_init_sta_addr);
      return ZX_OK;
    };

    auto result = client_.sync()->CreateIface(std::move(req_in));
    ASSERT_TRUE(result.ok());
  }
}

TEST_F(WlanphyDeviceTest, DestroyIfaceTest) {
  ConnectToWlanphyDevice();
  fuchsia_wlan_device::wire::DestroyIfaceRequest req_in = {
      .id = kFakeIfaceId,
  };

  fake_wlanphy_impl_protocol_ops_.destroy_iface = [](void* ctx, uint16_t id) -> zx_status_t {
    EXPECT_EQ(kFakeIfaceId, id);
    return ZX_OK;
  };
  auto result = client_.sync()->DestroyIface(std::move(req_in));
  ASSERT_TRUE(result.ok());
}

TEST_F(WlanphyDeviceTest, SetCountryTest) {
  ConnectToWlanphyDevice();
  fuchsia_wlan_device::wire::CountryCode cc_in = {
      .alpha2 =
          {
              .data_ = {'U', 'S'},
          },
  };
  fake_wlanphy_impl_protocol_ops_.set_country =
      [](void* ctx, const wlanphy_country_t* country) -> zx_status_t {
    EXPECT_EQ('U', country->alpha2[0]);
    EXPECT_EQ('S', country->alpha2[1]);
    return ZX_OK;
  };

  auto result = client_.sync()->SetCountry(std::move(cc_in));
  ASSERT_TRUE(result.ok());
}

TEST_F(WlanphyDeviceTest, GetCountryConvertsPrintableAndReturnsSuccess) {
  fake_wlanphy_impl_protocol_ops_.get_country = [](void* ctx, wlanphy_country_t* out_country) {
    *out_country = {{'U', 'S'}};
    return ZX_OK;
  };

  ConnectToWlanphyDevice();
  auto result = client_.sync()->GetCountry();
  ASSERT_TRUE(result.ok());

  EXPECT_EQ('U', result->value()->resp.alpha2.data()[0]);
  EXPECT_EQ('S', result->value()->resp.alpha2.data()[1]);
}

TEST_F(WlanphyDeviceTest, GetCountryConvertsNonPrintableAndReturnSuccess) {
  fake_wlanphy_impl_protocol_ops_.get_country = [](void* ctx, wlanphy_country_t* out_country) {
    *out_country = {{0x00, 0xff}};
    return ZX_OK;
  };

  ConnectToWlanphyDevice();
  auto result = client_.sync()->GetCountry();
  ASSERT_TRUE(result.ok());

  EXPECT_EQ(0x00, result->value()->resp.alpha2.data()[0]);
  EXPECT_EQ(0xff, result->value()->resp.alpha2.data()[1]);
}

TEST_F(WlanphyDeviceTest, ClearCountryTest) {
  ConnectToWlanphyDevice();
  fake_wlanphy_impl_protocol_ops_.clear_country = [](void* ctx) -> zx_status_t { return ZX_OK; };

  auto result = client_.sync()->ClearCountry();
  ASSERT_TRUE(result.ok());
}

TEST_F(WlanphyDeviceTest, SetPsModeTest) {
  ConnectToWlanphyDevice();
  fuchsia_wlan_common::wire::PowerSaveType ps_mode_in =
      fuchsia_wlan_common::wire::PowerSaveType::kPsModeLowPower;

  fake_wlanphy_impl_protocol_ops_.set_ps_mode =
      [](void* ctx, const wlanphy_ps_mode_t* ps_mode) -> zx_status_t {
    EXPECT_EQ(POWER_SAVE_TYPE_PS_MODE_LOW_POWER, ps_mode->ps_mode);
    return ZX_OK;
  };
  auto result = client_.sync()->SetPsMode(std::move(ps_mode_in));
  ASSERT_TRUE(result.ok());
}

TEST_F(WlanphyDeviceTest, GetPsModeReturnsSuccess) {
  fake_wlanphy_impl_protocol_ops_.get_ps_mode = [](void* ctx, wlanphy_ps_mode_t* ps_mode) {
    ps_mode->ps_mode = POWER_SAVE_TYPE_PS_MODE_BALANCED;
    return ZX_OK;
  };

  ConnectToWlanphyDevice();
  auto result = client_.sync()->GetPsMode();
  ASSERT_TRUE(result.ok());

  EXPECT_EQ(fuchsia_wlan_common::wire::PowerSaveType::kPsModeBalanced, result->value()->resp);
}

}  // namespace
}  // namespace wlanphy
