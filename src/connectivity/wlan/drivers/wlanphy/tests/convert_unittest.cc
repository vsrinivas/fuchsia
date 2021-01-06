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

template <typename T>
static inline constexpr bool is_power_of_two(T v) {
  static_assert(std::is_integral<T>::value, "T must be an integral type.");
  return (v > 0) && !(v & (v - 1));
}

TEST(WlanphyTest, is_power_of_two) {
  // All uint32_t powers of two should return true
  for (int i = 0; i < 32; i++) {
    EXPECT_TRUE(is_power_of_two((uint32_t)1 << i));
  }
  // Zero, negative numbers, any byte representation of a negative number, and
  // all other positive numbers are not powers of two.
  EXPECT_FALSE(is_power_of_two(0));
  EXPECT_FALSE(is_power_of_two(-1));
  EXPECT_TRUE(is_power_of_two((uint8_t)0b10000000));
  EXPECT_FALSE(is_power_of_two((int8_t)0b10000000));
  EXPECT_FALSE(is_power_of_two(754));
}

TEST(WlanphyTest, ConvertPhyRolesInfo) {
  constexpr wlan_info_mac_role_t kClient = WLAN_INFO_MAC_ROLE_CLIENT;
  constexpr wlan_info_mac_role_t kAp = WLAN_INFO_MAC_ROLE_AP;
  constexpr wlan_info_mac_role_t kMesh = WLAN_INFO_MAC_ROLE_MESH;
  constexpr wlan_info_mac_role_t kClientAp = WLAN_INFO_MAC_ROLE_CLIENT | WLAN_INFO_MAC_ROLE_AP;
  constexpr wlan_info_mac_role_t kClientApMesh =
      WLAN_INFO_MAC_ROLE_CLIENT | WLAN_INFO_MAC_ROLE_AP | WLAN_INFO_MAC_ROLE_MESH;

  // Check that each role only occupies one bitfield
  EXPECT_TRUE(is_power_of_two(WLAN_INFO_MAC_ROLE_CLIENT));
  EXPECT_TRUE(is_power_of_two(WLAN_INFO_MAC_ROLE_AP));
  EXPECT_TRUE(is_power_of_two(WLAN_INFO_MAC_ROLE_MESH));

  std::vector<wlan_device::MacRole> roles;

  // Check the return value of the function for each role and some combinations
  // Client
  ConvertPhyRolesInfo(&roles, kClient);
  EXPECT_NE(std::find(roles.begin(), roles.end(), wlan_device::MacRole::CLIENT), roles.end());
  // AP
  ConvertPhyRolesInfo(&roles, kAp);
  EXPECT_NE(std::find(roles.begin(), roles.end(), wlan_device::MacRole::AP), roles.end());
  // Mesh
  ConvertPhyRolesInfo(&roles, kMesh);
  EXPECT_NE(std::find(roles.begin(), roles.end(), wlan_device::MacRole::MESH), roles.end());
  // Client + AP
  ConvertPhyRolesInfo(&roles, kClientAp);
  EXPECT_NE(std::find(roles.begin(), roles.end(), wlan_device::MacRole::CLIENT), roles.end());
  EXPECT_NE(std::find(roles.begin(), roles.end(), wlan_device::MacRole::AP), roles.end());
  // Client + AP + Mesh
  ConvertPhyRolesInfo(&roles, kClientApMesh);
  EXPECT_NE(std::find(roles.begin(), roles.end(), wlan_device::MacRole::CLIENT), roles.end());
  EXPECT_NE(std::find(roles.begin(), roles.end(), wlan_device::MacRole::AP), roles.end());
  EXPECT_NE(std::find(roles.begin(), roles.end(), wlan_device::MacRole::MESH), roles.end());
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

}  // namespace
}  // namespace wlanphy
