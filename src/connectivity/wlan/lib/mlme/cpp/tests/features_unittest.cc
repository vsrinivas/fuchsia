// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/wlan/common/c/banjo.h>
#include <fuchsia/wlan/common/cpp/fidl.h>
#include <zircon/status.h>

#include <cstring>

#include <gtest/gtest.h>
#include <wlan/common/features.h>

namespace wlan::common {

namespace {

namespace fidl_common = ::fuchsia::wlan::common;

// DDK and FIDL versions representing the same discovery features.
const discovery_support_t kDiscoverySupportDdk{.scan_offload = {.supported = true},
                                               .probe_response_offload = {.supported = true}};
const fidl_common::DiscoverySupport kDiscoverySupportFidl{
    .scan_offload = {.supported = true}, .probe_response_offload = {.supported = true}};

// DDK and FIDL versions representing the same MAC sublayer features.
const mac_sublayer_support_t kMacSublayerSupportDdk{
    .rate_selection_offload = {.supported = true},
    .data_plane = {.data_plane_type = DATA_PLANE_TYPE_GENERIC_NETWORK_DEVICE},
    .device = {.is_synthetic = true,
               .mac_implementation_type = MAC_IMPLEMENTATION_TYPE_SOFTMAC,
               .tx_status_report_supported = true},
};

const fidl_common::MacSublayerSupport kMacSublayerSupportFidl{
    .rate_selection_offload = {.supported = true},
    .data_plane = {.data_plane_type = fidl_common::DataPlaneType::GENERIC_NETWORK_DEVICE},
    .device = {.is_synthetic = true,
               .mac_implementation_type = fidl_common::MacImplementationType::SOFTMAC,
               .tx_status_report_supported = true},
};

// DDK and FIDL versions representing the same security features.
const security_support_t kSecuritySupportDdk{.sae = {.supported = true, .handler = SAE_HANDLER_SME},
                                             .mfp = {.supported = true}};

const fidl_common::SecuritySupport kSecuritySupportFidl{
    .sae = {.supported = true, .handler = fidl_common::SaeHandler::SME},
    .mfp = {.supported = true}};

// DDK and FIDL versions representing the same spectrum management features.
const spectrum_management_support_t kSpectrumManagementSupportDdk{.dfs = {.supported = true}};

const fidl_common::SpectrumManagementSupport kSpectrumManagementSupportFidl{
    .dfs = {.supported = true}};

TEST(DiscoverySupportConversionTest, DdkToFidl) {
  const fidl_common::DiscoverySupport& expected = kDiscoverySupportFidl;
  fidl_common::DiscoverySupport actual;
  ASSERT_EQ(ConvertDiscoverySupportToFidl(kDiscoverySupportDdk, &actual), ZX_OK);
  EXPECT_EQ(actual.scan_offload.supported, expected.scan_offload.supported);
  EXPECT_EQ(actual.probe_response_offload.supported, expected.probe_response_offload.supported);
}

TEST(DiscoverySupportConversionTest, FidlToDdk) {
  const discovery_support_t& expected = kDiscoverySupportDdk;
  discovery_support_t actual;
  ASSERT_EQ(ConvertDiscoverySupportToDdk(kDiscoverySupportFidl, &actual), ZX_OK);
  EXPECT_EQ(actual.scan_offload.supported, expected.scan_offload.supported);
  EXPECT_EQ(actual.probe_response_offload.supported, expected.probe_response_offload.supported);
}

TEST(MacSublayerSupportConversionTest, DdkToFidl) {
  const fidl_common::MacSublayerSupport& expected = kMacSublayerSupportFidl;
  fidl_common::MacSublayerSupport actual;
  ASSERT_EQ(ConvertMacSublayerSupportToFidl(kMacSublayerSupportDdk, &actual), ZX_OK);
  EXPECT_EQ(actual.rate_selection_offload.supported, expected.rate_selection_offload.supported);
  EXPECT_EQ(actual.data_plane.data_plane_type, expected.data_plane.data_plane_type);
  EXPECT_EQ(actual.device.is_synthetic, expected.device.is_synthetic);
  EXPECT_EQ(actual.device.mac_implementation_type, expected.device.mac_implementation_type);
  EXPECT_EQ(actual.device.tx_status_report_supported, expected.device.tx_status_report_supported);
}

TEST(MacSublayerSupportConversionTest, FidlToDdk) {
  const mac_sublayer_support_t& expected = kMacSublayerSupportDdk;
  mac_sublayer_support_t actual;
  ASSERT_EQ(ConvertMacSublayerSupportToDdk(kMacSublayerSupportFidl, &actual), ZX_OK);
  EXPECT_EQ(actual.rate_selection_offload.supported, expected.rate_selection_offload.supported);
  EXPECT_EQ(actual.data_plane.data_plane_type, expected.data_plane.data_plane_type);
  EXPECT_EQ(actual.device.is_synthetic, expected.device.is_synthetic);
  EXPECT_EQ(actual.device.mac_implementation_type, expected.device.mac_implementation_type);
  EXPECT_EQ(actual.device.tx_status_report_supported, expected.device.tx_status_report_supported);
}

TEST(MacSublayerSupportConversionTest, InvalidDdkInputRecognized) {
  // Create a malformed data structure.
  mac_sublayer_support_t invalid;
  memcpy(&invalid, &kMacSublayerSupportDdk, sizeof(kMacSublayerSupportDdk));
  const uint8_t invalid_data_plane_type = 0;
  invalid.data_plane.data_plane_type = invalid_data_plane_type;

  fidl_common::MacSublayerSupport actual;
  ASSERT_NE(ConvertMacSublayerSupportToFidl(invalid, &actual), ZX_OK);
}

TEST(SecuritySupportConversionTest, DdkToFidl) {
  const fidl_common::SecuritySupport& expected = kSecuritySupportFidl;
  fidl_common::SecuritySupport actual;
  ASSERT_EQ(ConvertSecuritySupportToFidl(kSecuritySupportDdk, &actual), ZX_OK);
  EXPECT_EQ(actual.sae.supported, expected.sae.supported);
  EXPECT_EQ(actual.sae.handler, expected.sae.handler);
  EXPECT_EQ(actual.mfp.supported, expected.mfp.supported);
}

TEST(SecuritySupportConversionTest, FidlToDdk) {
  const security_support_t& expected = kSecuritySupportDdk;
  security_support_t actual;
  ASSERT_EQ(ConvertSecuritySupportToDdk(kSecuritySupportFidl, &actual), ZX_OK);
  EXPECT_EQ(actual.sae.supported, expected.sae.supported);
  EXPECT_EQ(actual.sae.handler, expected.sae.handler);
  EXPECT_EQ(actual.mfp.supported, expected.mfp.supported);
}

TEST(SecuritySupportConversionTest, InvalidDdkInputRecognized) {
  // Create a malformed data structure.
  security_support_t invalid;
  memcpy(&invalid, &kSecuritySupportDdk, sizeof(kSecuritySupportDdk));
  const uint8_t invalid_sae_handler = 0;
  invalid.sae.handler = invalid_sae_handler;

  fidl_common::SecuritySupport actual;
  ASSERT_NE(ConvertSecuritySupportToFidl(invalid, &actual), ZX_OK);
}

TEST(SpectrumManagementSupportConversionTest, DdkToFidl) {
  const fidl_common::SpectrumManagementSupport& expected = kSpectrumManagementSupportFidl;
  fidl_common::SpectrumManagementSupport actual;
  ASSERT_EQ(ConvertSpectrumManagementSupportToFidl(kSpectrumManagementSupportDdk, &actual), ZX_OK);
  EXPECT_EQ(actual.dfs.supported, expected.dfs.supported);
}

TEST(SpectrumManagementSupportConversionTest, FidlToDdk) {
  const spectrum_management_support_t& expected = kSpectrumManagementSupportDdk;
  spectrum_management_support_t actual;
  ASSERT_EQ(ConvertSpectrumManagementSupportToDdk(kSpectrumManagementSupportFidl, &actual), ZX_OK);
  EXPECT_EQ(actual.dfs.supported, expected.dfs.supported);
}

}  // namespace

}  // namespace wlan::common
