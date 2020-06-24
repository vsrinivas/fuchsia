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

TEST(WlanphyTest, ConvertPhyBandInfo) {
  wlan_info_band_info_t in[WLAN_INFO_MAX_BANDS];
  in[0] = {};
  in[1] = {};

  in[0].band = WLAN_INFO_BAND_2GHZ;
  in[1].band = WLAN_INFO_BAND_5GHZ;

  in[0].ht_supported = false;
  in[1].ht_supported = true;

  in[0].vht_supported = false;
  in[1].vht_supported = true;

  for (size_t i = 0; i < 10; i++) {
    in[0].rates[i] = i + 1;
    in[1].rates[i] = 101 + i;
  }
  in[0].rates[10] = 11;

  in[0].supported_channels.base_freq = 65533;
  in[1].supported_channels.base_freq = 65534;

  for (uint8_t i = 0; i < WLAN_INFO_CHANNEL_LIST_MAX_CHANNELS; i++) {
    if (i < 32) {
      in[0].supported_channels.channels[i] = 11 + i;
    }
    in[1].supported_channels.channels[i] = 22 + i;
  }

  std::vector<wlan_device::BandInfo> out;
  ConvertPhyBandInfo(&out, WLAN_INFO_MAX_BANDS, in);
  ASSERT_EQ(out.size(), 2ul);
  EXPECT_EQ(out[0].band_id, wlan_common::Band::WLAN_BAND_2GHZ);
  EXPECT_EQ(out[1].band_id, wlan_common::Band::WLAN_BAND_5GHZ);
  EXPECT_EQ(out[0].ht_caps.get(), nullptr);
  EXPECT_NE(out[1].ht_caps.get(), nullptr);
  EXPECT_EQ(out[0].vht_caps.get(), nullptr);
  EXPECT_NE(out[1].vht_caps.get(), nullptr);
  std::vector<uint8_t> expected_rates_2g = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
  std::vector<uint8_t> expected_rates_5g = {101, 102, 103, 104, 105, 106, 107, 108, 109, 110};
  EXPECT_EQ(out[0].rates, expected_rates_2g);
  EXPECT_EQ(out[1].rates, expected_rates_5g);

  EXPECT_EQ(out[0].supported_channels.base_freq, 65533);
  EXPECT_EQ(out[1].supported_channels.base_freq, 65534);
  std::vector<uint8_t> expected_channels_2g = {11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21,
                                               22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32,
                                               33, 34, 35, 36, 37, 38, 39, 40, 41, 42};
  std::vector<uint8_t> expected_channels_5g = {
      22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43,
      44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65,
      66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85};
  EXPECT_EQ(out[0].supported_channels.channels, expected_channels_2g);
  EXPECT_EQ(out[1].supported_channels.channels, expected_channels_5g);
}

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

TEST(WlanphyTest, ConvertPhyCaps) {
  std::vector<wlan_device::Capability> caps;
  uint32_t phy_caps_mask = WLAN_INFO_HARDWARE_CAPABILITY_SHORT_SLOT_TIME |
                           WLAN_INFO_HARDWARE_CAPABILITY_SIMULTANEOUS_CLIENT_AP;

  ConvertPhyCaps(&caps, phy_caps_mask);
  EXPECT_NE(std::find(caps.begin(), caps.end(), wlan_device::Capability::SHORT_SLOT_TIME),
            caps.end());
  EXPECT_NE(std::find(caps.begin(), caps.end(), wlan_device::Capability::SIMULTANEOUS_CLIENT_AP),
            caps.end());
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
