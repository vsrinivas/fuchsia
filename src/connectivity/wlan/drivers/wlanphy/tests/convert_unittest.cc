// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/wlan/common/cpp/fidl.h>
#include <fuchsia/wlan/device/cpp/fidl.h>

#include <gtest/gtest.h>

#include "../device.h"
#include "../driver.h"

namespace wlanphy {
namespace {
namespace wlan_common = ::fuchsia::wlan::common;
namespace wlan_device = ::fuchsia::wlan::device;

TEST(WlanphyTest, ConvertSupportedMacRoles) {
  const wlan_mac_role_t kClient[] = {WLAN_MAC_ROLE_CLIENT};
  const wlan_mac_role_t kAp[] = {WLAN_MAC_ROLE_AP};
  const wlan_mac_role_t kMesh[] = {WLAN_MAC_ROLE_MESH};
  const wlan_mac_role_t kClientAp[] = {WLAN_MAC_ROLE_CLIENT, WLAN_MAC_ROLE_AP};
  const wlan_mac_role_t kClientApMesh[] = {WLAN_MAC_ROLE_CLIENT, WLAN_MAC_ROLE_AP,
                                           WLAN_MAC_ROLE_MESH};

  std::vector<wlan_common::WlanMacRole> roles;

  // Check the return value of the function for each role and some combinations
  // Client
  ConvertSupportedMacRoles(&roles, kClient, 1);
  EXPECT_NE(std::find(roles.begin(), roles.end(), wlan_common::WlanMacRole::CLIENT), roles.end());
  // AP
  ConvertSupportedMacRoles(&roles, kAp, 1);
  EXPECT_NE(std::find(roles.begin(), roles.end(), wlan_common::WlanMacRole::AP), roles.end());
  // Mesh
  ConvertSupportedMacRoles(&roles, kMesh, 1);
  EXPECT_NE(std::find(roles.begin(), roles.end(), wlan_common::WlanMacRole::MESH), roles.end());
  // Client + AP
  ConvertSupportedMacRoles(&roles, kClientAp, 2);
  EXPECT_NE(std::find(roles.begin(), roles.end(), wlan_common::WlanMacRole::CLIENT), roles.end());
  EXPECT_NE(std::find(roles.begin(), roles.end(), wlan_common::WlanMacRole::AP), roles.end());
  // Client + AP + Mesh
  ConvertSupportedMacRoles(&roles, kClientApMesh, 3);
  EXPECT_NE(std::find(roles.begin(), roles.end(), wlan_common::WlanMacRole::CLIENT), roles.end());
  EXPECT_NE(std::find(roles.begin(), roles.end(), wlan_common::WlanMacRole::AP), roles.end());
  EXPECT_NE(std::find(roles.begin(), roles.end(), wlan_common::WlanMacRole::MESH), roles.end());
}

wlanphy_impl_protocol_ops_t make_ops_for_get_country(
    zx_status_t (*get_country)(void* ctx, wlanphy_country_t* out_country)) {
  return wlanphy_impl_protocol_ops_t{
      .query = [](void* ctx, wlanphy_impl_info_t* info) -> zx_status_t {
        return ZX_ERR_NOT_SUPPORTED;
      },
      .create_iface = [](void* ctx, const wlanphy_impl_create_iface_req_t* req,
                         uint16_t* out_iface_id) -> zx_status_t { return ZX_ERR_NOT_SUPPORTED; },
      .destroy_iface = [](void* ctx, uint16_t id) -> zx_status_t { return ZX_ERR_NOT_SUPPORTED; },
      .set_country = [](void* ctx, const wlanphy_country_t* country) -> zx_status_t {
        return ZX_ERR_NOT_SUPPORTED;
      },
      .get_country = get_country,
      .set_ps_mode = [](void* ctx, const wlanphy_ps_mode_t* ps_mode) -> zx_status_t {
        return ZX_ERR_NOT_SUPPORTED;
      },
      .get_ps_mode = [](void* ctx, wlanphy_ps_mode_t* ps_mode) -> zx_status_t {
        return ZX_ERR_NOT_SUPPORTED;
      },
  };
}

TEST(WlanphyTest, GetCountryConvertsPrintableAndReturnsSuccess) {
  auto ops = make_ops_for_get_country([](void* ctx, wlanphy_country_t* out_country) {
    *out_country = {{'U', 'S'}};
    return ZX_OK;
  });
  void* ctx = nullptr;
  wlanphy_init(&ctx);

  Device dev(nullptr, wlanphy_impl_protocol_t{.ops = &ops, .ctx = ctx});
  bool invoked_callback = false;
  dev.GetCountry([&invoked_callback](fuchsia::wlan::device::Phy_GetCountry_Result result) {
    fuchsia::wlan::device::CountryCode expected_country = {'U', 'S'};
    ASSERT_TRUE(result.is_response());
    EXPECT_EQ(result.response().resp.alpha2, expected_country.alpha2);
    invoked_callback = true;
  });
  EXPECT_EQ(invoked_callback, true);
}

TEST(WlanphyTest, GetCountryConvertsNonPrintableAndReturnSuccess) {
  auto ops = make_ops_for_get_country([](void* ctx, wlanphy_country_t* out_country) {
    *out_country = {{0x00, 0xff}};
    return ZX_OK;
  });
  void* ctx = nullptr;
  wlanphy_init(&ctx);

  Device dev(nullptr, wlanphy_impl_protocol_t{.ops = &ops, .ctx = ctx});
  bool invoked_callback = false;
  dev.GetCountry([&invoked_callback](fuchsia::wlan::device::Phy_GetCountry_Result result) {
    fuchsia::wlan::device::CountryCode expected_country = {0x00, 0xff};
    ASSERT_TRUE(result.is_response());
    EXPECT_EQ(result.response().resp.alpha2, expected_country.alpha2);
    invoked_callback = true;
  });
  EXPECT_EQ(invoked_callback, true);
}

wlanphy_impl_protocol_ops_t make_ops_for_get_ps_mode(
    zx_status_t (*get_ps_mode)(void* ctx, wlanphy_ps_mode_t* ps_mode)) {
  return wlanphy_impl_protocol_ops_t{
      .query = [](void* ctx, wlanphy_impl_info_t* info) -> zx_status_t {
        return ZX_ERR_NOT_SUPPORTED;
      },
      .create_iface = [](void* ctx, const wlanphy_impl_create_iface_req_t* req,
                         uint16_t* out_iface_id) -> zx_status_t { return ZX_ERR_NOT_SUPPORTED; },
      .destroy_iface = [](void* ctx, uint16_t id) -> zx_status_t { return ZX_ERR_NOT_SUPPORTED; },
      .set_country = [](void* ctx, const wlanphy_country_t* country) -> zx_status_t {
        return ZX_ERR_NOT_SUPPORTED;
      },
      .get_country = [](void* ctx, wlanphy_country_t* country) -> zx_status_t {
        return ZX_ERR_NOT_SUPPORTED;
      },
      .set_ps_mode = [](void* ctx, const wlanphy_ps_mode_t* ps_mode) -> zx_status_t {
        return ZX_ERR_NOT_SUPPORTED;
      },
      .get_ps_mode = get_ps_mode,
  };
}

TEST(WlanphyTest, GetPsModeReturnsSuccess) {
  auto ops = make_ops_for_get_ps_mode([](void* ctx, wlanphy_ps_mode_t* ps_mode) {
    ps_mode->ps_mode = POWER_SAVE_TYPE_FAST_PS_MODE;
    return ZX_OK;
  });
  void* ctx = nullptr;
  wlanphy_init(&ctx);

  Device dev(nullptr, wlanphy_impl_protocol_t{.ops = &ops, .ctx = ctx});
  bool invoked_callback = false;
  dev.GetPsMode([&invoked_callback](fuchsia::wlan::device::Phy_GetPsMode_Result result) {
    constexpr power_save_type_t exp_ps_mode = POWER_SAVE_TYPE_FAST_PS_MODE;
    EXPECT_EQ((power_save_type_t)result.response().resp, exp_ps_mode);
    invoked_callback = true;
  });
  EXPECT_EQ(invoked_callback, true);
}

}  // namespace
}  // namespace wlanphy
