// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/wlan/ieee80211/cpp/fidl.h>
#include <fuchsia/wlan/stats/cpp/fidl.h>

#include <gtest/gtest.h>
#include <src/connectivity/wlan/drivers/wlan/convert.h>

namespace wlan {
namespace {
namespace wlan_softmac = fuchsia_wlan_softmac::wire;
namespace wlan_common = fuchsia_wlan_common::wire;
namespace wlan_ieee80211 = fuchsia_wlan_ieee80211::wire;
namespace wlan_internal = fuchsia_wlan_internal::wire;
namespace wlan_associnfo = fuchsia_hardware_wlan_associnfo::wire;

/* Metadata which is used as input and expected output for the under-test conversion functions*/

// Fake metadata -- general
static constexpr uint8_t kFakeMacAddr[wlan_ieee80211::kMacAddrLen] = {6, 5, 4, 3, 2, 2};
static constexpr uint8_t kFakeRate = 206;
static constexpr size_t kFakeAssocCtxRateCount = 263;
static constexpr size_t kFakeMcsSetLen = 16;
static constexpr uint8_t kFakeHtCapBytes[wlan_ieee80211::kHtCapLen] = {
    3, 1, 4, 1, 5, 9, 2, 6, 5, 3, 5, 8, 9, 7, 9, 3, 2, 3, 8, 4, 6, 2, 6, 4, 3, 3};
static constexpr uint8_t kFakeVhtCapBytes[wlan_ieee80211::kVhtCapLen] = {8, 3, 2, 7, 9, 5,
                                                                         0, 2, 8, 8, 4, 1};
static constexpr uint8_t kFakeOui[wlan_ieee80211::kOuiLen] = {9, 7, 1};
static constexpr uint8_t kFakeChannel = 15;
static constexpr zx_duration_t kFakeDuration = 4567;
static constexpr uint8_t kFakeSsidLen = 9;
static constexpr uint8_t kFakeSsid[kFakeSsidLen] = {'w', 'h', 'a', 't', 'a', 't', 'e', 's', 't'};
static constexpr uint8_t kFakeKey[wlan_ieee80211::kMaxKeyLen] = {
    6, 9, 3, 9, 9, 3, 7, 5, 1, 0, 5, 8, 2, 0, 9, 7, 4, 9, 4, 4, 5, 9, 2, 3, 0, 7, 8, 1, 6, 4, 0, 6};
static constexpr size_t kFakePacketSize = 50;

static constexpr bool kPopulaterBool = true;
static constexpr uint8_t kRandomPopulaterUint8 = 118;
static constexpr uint16_t kRandomPopulaterUint16 = 53535;
static constexpr uint32_t kRandomPopulaterUint32 = 4062722468;
static constexpr uint64_t kRandomPopulaterUint64 = 1518741085930693;
static constexpr int8_t kRandomPopulaterInt8 = -95;
static constexpr int16_t kRandomPopulaterInt16 = -24679;

// Fake metadata -- FIDL
static constexpr wlan_common::WlanMacRole kFakeFidlMacRole = wlan_common::WlanMacRole::kAp;
static constexpr wlan_common::WlanPhyType kFakeFidlPhyType = wlan_common::WlanPhyType::kErp;
static constexpr wlan_common::WlanSoftmacHardwareCapabilityBit
    kFakeFidlSoftmacHardwareCapabilityBit =
        wlan_common::WlanSoftmacHardwareCapabilityBit::kSpectrumMgmt;
static constexpr wlan_common::WlanBand kFakeFidlBand = wlan_common::WlanBand::kFiveGhz;
static constexpr wlan_common::ChannelBandwidth kFakeFidlChannelBandwidth =
    wlan_common::ChannelBandwidth::kCbw160;
static constexpr wlan_softmac::WlanProtection kFakeFidlProtection =
    wlan_softmac::WlanProtection::kRxTx;
static constexpr wlan_associnfo::WlanKeyType kFakeFidlKeyType = wlan_associnfo::WlanKeyType::kGroup;
static constexpr wlan_internal::BssType kFakeFidlBssType = wlan_internal::BssType::kMesh;
static constexpr wlan_common::WlanTxResult kFakeFidlTxResult = wlan_common::WlanTxResult::kSuccess;
static constexpr wlan_common::DataPlaneType kFakeFidlDataPlaneType =
    wlan_common::DataPlaneType::kEthernetDevice;
static constexpr wlan_common::MacImplementationType kFakeFidlMacImplementationType =
    wlan_common::MacImplementationType::kFullmac;

// Fake metadata -- banjo
static constexpr uint32_t kFakeBanjoMacRole = WLAN_MAC_ROLE_AP;
static constexpr uint32_t kFakeBanjoPhyType = WLAN_PHY_TYPE_ERP;
static constexpr uint32_t kFakeBanjoSoftmacHardwareCapabilityBit =
    WLAN_SOFTMAC_HARDWARE_CAPABILITY_BIT_SPECTRUM_MGMT;
static constexpr uint8_t kFakeBanjoBand = WLAN_BAND_FIVE_GHZ;
static constexpr uint32_t kFakeBanjoChannelBandwidth = CHANNEL_BANDWIDTH_CBW160;
static constexpr uint8_t kFakeBanjoProtection = WLAN_PROTECTION_RX_TX;
static constexpr uint8_t kFakeBanjoKeyType = WLAN_KEY_TYPE_GROUP;
static constexpr uint32_t kFakeBanjoBssType = BSS_TYPE_MESH;
static constexpr uint8_t kFakeBanjoTxResult = WLAN_TX_RESULT_SUCCESS;
static constexpr uint8_t kFakeBanjoDataPlaneType = DATA_PLANE_TYPE_ETHERNET_DEVICE;
static constexpr uint8_t kFakeBanjoMacImplementationType = MAC_IMPLEMENTATION_TYPE_FULLMAC;

/* Test cases*/

// FIDL to banjo types tests.
TEST(ConvertTest, ToBanjoWlanSoftmacInfo) {
  // Build WlanSoftmacInfo
  fidl::Arena arena;
  auto builder = wlan_softmac::WlanSoftmacInfo::Builder(arena);

  fidl::Array<uint8_t, wlan_ieee80211::kMacAddrLen> sta_addr;
  memcpy(sta_addr.begin(), kFakeMacAddr, sta_addr.size());
  builder.sta_addr(sta_addr);

  builder.mac_role(kFakeFidlMacRole);

  std::vector<wlan_common::WlanPhyType> phy_vec;
  for (size_t i = 0; i < wlan_common::kMaxSupportedPhyTypes; i++) {
    phy_vec.push_back(kFakeFidlPhyType);
  }
  builder.supported_phys(fidl::VectorView<wlan_common::WlanPhyType>(arena, phy_vec));

  builder.hardware_capability((uint32_t)kFakeFidlSoftmacHardwareCapabilityBit);

  wlan_softmac::WlanSoftmacBandCapability band_caps_buffer[wlan_common::kMaxBands];

  for (size_t i = 0; i < wlan_common::kMaxBands; i++) {
    auto& band_cap = band_caps_buffer[i];

    band_cap.band = kFakeFidlBand;
    band_cap.basic_rate_count = wlan_internal::kMaxSupportedBasicRates;

    for (size_t j = 0; j < wlan_internal::kMaxSupportedBasicRates; j++) {
      band_cap.basic_rate_list.data()[j] = kFakeRate;
    }

    band_cap.ht_supported = kPopulaterBool;
    memcpy(band_cap.ht_caps.bytes.data(), kFakeHtCapBytes, wlan_ieee80211::kHtCapLen);
    band_cap.vht_supported = kPopulaterBool;
    memcpy(band_cap.vht_caps.bytes.data(), kFakeVhtCapBytes, wlan_ieee80211::kVhtCapLen);

    band_cap.operating_channel_count = wlan_ieee80211::kMaxUniqueChannelNumbers;
    for (size_t j = 0; j < wlan_ieee80211::kMaxUniqueChannelNumbers; j++) {
      band_cap.operating_channel_list[j] = kFakeChannel;
    }
  }
  builder.band_caps(
      fidl::VectorView<wlan_softmac::WlanSoftmacBandCapability>(arena, band_caps_buffer));
  auto in = builder.Build();

  // Conduct conversion
  wlan_softmac_info_t out;
  ConvertWlanSoftmacInfo(in, &out);

  // Verify outputs
  EXPECT_EQ(0, memcmp(out.sta_addr, kFakeMacAddr, wlan_ieee80211::kMacAddrLen));
  EXPECT_EQ(kFakeBanjoMacRole, out.mac_role);
  EXPECT_EQ(wlan_common::kMaxSupportedPhyTypes, out.supported_phys_count);
  for (size_t i = 0; i < out.supported_phys_count; i++) {
    EXPECT_EQ(kFakeBanjoPhyType, out.supported_phys_list[i]);
  }
  EXPECT_EQ(kFakeBanjoSoftmacHardwareCapabilityBit, out.hardware_capability);

  EXPECT_EQ(wlan_common::kMaxBands, out.band_cap_count);
  for (size_t i = 0; i < wlan_common::kMaxBands; i++) {
    auto band_cap = out.band_cap_list[i];
    EXPECT_EQ(kFakeBanjoBand, band_cap.band);
    EXPECT_EQ(wlan_internal::kMaxSupportedBasicRates, band_cap.basic_rate_count);
    for (size_t j = 0; j < wlan_internal::kMaxSupportedBasicRates; j++) {
      EXPECT_EQ(kFakeRate, band_cap.basic_rate_list[j]);
    }
    EXPECT_EQ(kPopulaterBool, band_cap.ht_supported);
    EXPECT_EQ(0, memcmp(band_cap.ht_caps.bytes, kFakeHtCapBytes, wlan_ieee80211::kHtCapLen));
    EXPECT_EQ(kPopulaterBool, band_cap.vht_supported);
    EXPECT_EQ(0, memcmp(band_cap.vht_caps.bytes, kFakeVhtCapBytes, wlan_ieee80211::kVhtCapLen));
    EXPECT_EQ(wlan_ieee80211::kMaxUniqueChannelNumbers, band_cap.operating_channel_count);
    for (size_t j = 0; j < wlan_ieee80211::kMaxUniqueChannelNumbers; j++) {
      EXPECT_EQ(kFakeChannel, band_cap.operating_channel_list[j]);
    }
  }
}

TEST(ConvertTest, ToBanjoDiscoverySuppport) {
  wlan_common::DiscoverySupport in = {
      .scan_offload =
          {
              .supported = kPopulaterBool,
          },
      .probe_response_offload =
          {
              .supported = kPopulaterBool,
          },
  };

  discovery_support_t out;
  ConvertDiscoverySuppport(in, &out);

  EXPECT_EQ(kPopulaterBool, out.scan_offload.supported);
  EXPECT_EQ(kPopulaterBool, out.probe_response_offload.supported);
}

TEST(ConvertTest, ToBanjoMacSublayerSupport) {
  wlan_common::MacSublayerSupport in = {
      .rate_selection_offload =
          {
              .supported = kPopulaterBool,
          },
      .data_plane =
          {
              .data_plane_type = kFakeFidlDataPlaneType,
          },
      .device =
          {
              .is_synthetic = kPopulaterBool,
              .mac_implementation_type = kFakeFidlMacImplementationType,
              .tx_status_report_supported = kPopulaterBool,
          },
  };

  mac_sublayer_support_t out;
  ConvertMacSublayerSupport(in, &out);

  EXPECT_EQ(kPopulaterBool, out.rate_selection_offload.supported);
  EXPECT_EQ(kFakeBanjoDataPlaneType, out.data_plane.data_plane_type);
  EXPECT_EQ(kPopulaterBool, out.device.is_synthetic);
  EXPECT_EQ(kFakeBanjoMacImplementationType, out.device.mac_implementation_type);
  EXPECT_EQ(kPopulaterBool, out.device.tx_status_report_supported);
}

TEST(ConvertTest, ToBanjoSecuritySupport) {
  wlan_common::SecuritySupport in = {
      .sae =
          {
              .driver_handler_supported = kPopulaterBool,
              .sme_handler_supported = kPopulaterBool,
          },
      .mfp =
          {
              .supported = kPopulaterBool,
          },
  };

  security_support_t out;
  ConvertSecuritySupport(in, &out);

  EXPECT_EQ(kPopulaterBool, out.sae.driver_handler_supported);
  EXPECT_EQ(kPopulaterBool, out.sae.sme_handler_supported);
  EXPECT_EQ(kPopulaterBool, out.mfp.supported);
}

TEST(ConvertTest, ToBanjoSpectrumManagementSupport) {
  wlan_common::SpectrumManagementSupport in = {
      .dfs =
          {
              .supported = kPopulaterBool,
          },
  };

  spectrum_management_support_t out;
  ConvertSpectrumManagementSupport(in, &out);

  EXPECT_EQ(kPopulaterBool, out.dfs.supported);
}

TEST(ConvertTest, ToBanjoRxPacket) {
  // Populate wlan_softmac::WlanRxPacket
  uint8_t* rx_packet = (uint8_t*)calloc(kFakePacketSize, sizeof(uint8_t));
  for (size_t i = 0; i < kFakePacketSize; i++) {
    rx_packet[i] = kRandomPopulaterUint8;
  }

  wlan_softmac::WlanRxPacket in = {
      .mac_frame = fidl::VectorView<uint8_t>::FromExternal(rx_packet, kFakePacketSize),
      .info =
          {
              .rx_flags = kRandomPopulaterUint32,
              .valid_fields = kRandomPopulaterUint32,
              .phy = kFakeFidlPhyType,
              .data_rate = kRandomPopulaterUint32,
              .channel =
                  {
                      .primary = kFakeChannel,
                      .cbw = kFakeFidlChannelBandwidth,
                      .secondary80 = kFakeChannel,
                  },
              .mcs = kRandomPopulaterUint8,
              .rssi_dbm = kRandomPopulaterInt8,
              .snr_dbh = kRandomPopulaterInt16,
          },
  };

  // Conduct conversion
  wlan_rx_packet_t out;
  EXPECT_EQ(ZX_OK, ConvertRxPacket(in, &out));

  // Verify outputs
  EXPECT_EQ(kFakePacketSize, out.mac_frame_size);
  for (size_t i = 0; i < kFakePacketSize; i++) {
    EXPECT_EQ(kRandomPopulaterUint8, out.mac_frame_buffer[i]);
  }

  EXPECT_EQ(kRandomPopulaterUint32, out.info.rx_flags);
  EXPECT_EQ(kRandomPopulaterUint32, out.info.valid_fields);
  EXPECT_EQ(kFakeBanjoPhyType, out.info.phy);
  EXPECT_EQ(kRandomPopulaterUint32, out.info.data_rate);
  EXPECT_EQ(kFakeChannel, out.info.channel.primary);
  EXPECT_EQ(kFakeBanjoChannelBandwidth, out.info.channel.cbw);
  EXPECT_EQ(kFakeChannel, out.info.channel.secondary80);
  EXPECT_EQ(kRandomPopulaterUint8, out.info.mcs);
  EXPECT_EQ(kRandomPopulaterInt8, out.info.rssi_dbm);
  EXPECT_EQ(kRandomPopulaterInt16, out.info.snr_dbh);

  free(rx_packet);
}  // namespace

TEST(ConvertTest, ToBanjoTxStatus) {
  // Populate wlan_common::WlanTxStatus
  wlan_common::WlanTxStatus in = {
      .result = kFakeFidlTxResult,
  };
  for (size_t i = 0; i < wlan_common::kWlanTxStatusMaxEntry; i++) {
    in.tx_status_entry[i].tx_vector_idx = kRandomPopulaterUint16;
    in.tx_status_entry[i].attempts = kRandomPopulaterUint8;
  }
  for (size_t i = 0; i < wlan_ieee80211::kMacAddrLen; i++) {
    in.peer_addr[i] = kFakeMacAddr[i];
  }

  // Conduct conversion
  wlan_tx_status_t out;
  EXPECT_EQ(ZX_OK, ConvertTxStatus(in, &out));

  // Verify outputs
  for (size_t i = 0; i < wlan_common::kWlanTxStatusMaxEntry; i++) {
    EXPECT_EQ(kRandomPopulaterUint16, out.tx_status_entry[i].tx_vector_idx);
    EXPECT_EQ(kRandomPopulaterUint8, out.tx_status_entry[i].attempts);
  }

  for (size_t i = 0; i < wlan_ieee80211::kMacAddrLen; i++) {
    EXPECT_EQ(kFakeMacAddr[i], out.peer_addr[i]);
  }
  EXPECT_EQ(kFakeBanjoTxResult, out.result);
}

// banjo to FIDL types tests.
TEST(ConvertTest, ToFidlMacRole) {
  wlan_common::WlanMacRole out;
  EXPECT_EQ(ZX_OK, ConvertMacRole(kFakeBanjoMacRole, &out));

  EXPECT_EQ(kFakeFidlMacRole, out);

  // Input the invalid value, and the conversion will fail.
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, ConvertMacRole(kRandomPopulaterUint32, &out));
}

TEST(ConvertTest, ToFidlTxPacket) {
  // Populate wlan_tx_info_t
  uint8_t* data_in = (uint8_t*)calloc(kFakePacketSize, sizeof(uint8_t));
  for (size_t i = 0; i < kFakePacketSize; i++) {
    data_in[i] = kRandomPopulaterUint8;
  }

  wlan_tx_info_t info_in = {
      .tx_flags = kRandomPopulaterUint8,
      .valid_fields = kRandomPopulaterUint32,
      .tx_vector_idx = kRandomPopulaterUint16,
      .phy = kFakeBanjoPhyType,  // Valid PhyType in first try.
      .channel_bandwidth = kFakeBanjoChannelBandwidth,
      .mcs = kRandomPopulaterUint8,
  };

  // Conduct conversion
  wlan_softmac::WlanTxPacket out;
  EXPECT_EQ(ZX_OK, ConvertTxPacket(data_in, kFakePacketSize, info_in, &out));

  // Verify outputs
  EXPECT_EQ(kFakePacketSize, out.mac_frame.count());
  for (size_t i = 0; i < kFakePacketSize; i++) {
    EXPECT_EQ(kRandomPopulaterUint8, out.mac_frame.data()[i]);
  }

  EXPECT_EQ(kRandomPopulaterUint8, out.info.tx_flags);
  EXPECT_EQ(kRandomPopulaterUint32, out.info.valid_fields);
  EXPECT_EQ(kRandomPopulaterUint16, out.info.tx_vector_idx);
  EXPECT_EQ(kFakeFidlPhyType, out.info.phy);
  EXPECT_EQ(kFakeFidlChannelBandwidth, out.info.channel_bandwidth);
  EXPECT_EQ(kRandomPopulaterUint8, out.info.mcs);

  // Assign invalid values to the enum fields and verify the error returned.
  info_in.phy = kRandomPopulaterUint32;
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, ConvertTxPacket(data_in, kFakePacketSize, info_in, &out));

  info_in.phy = kFakeBanjoPhyType;
  info_in.channel_bandwidth = kRandomPopulaterUint32;
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, ConvertTxPacket(data_in, kFakePacketSize, info_in, &out));

  free(data_in);
}

TEST(ConvertTest, ToFidlChannel) {
  wlan_channel_t in = {
      .primary = kFakeChannel,
      .cbw = kFakeBanjoChannelBandwidth,
      .secondary80 = kFakeChannel,
  };

  wlan_common::WlanChannel out;
  EXPECT_EQ(ZX_OK, ConvertChannel(in, &out));

  EXPECT_EQ(kFakeChannel, out.primary);
  EXPECT_EQ(kFakeFidlChannelBandwidth, out.cbw);
  EXPECT_EQ(kFakeChannel, out.secondary80);

  // Assign an invalid value to cbw, and the conversion will fail.
  in.cbw = kRandomPopulaterUint32;
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, ConvertChannel(in, &out));
}

TEST(ConvertTest, ToFidlBssConfig) {
  // Populate bss_config_t
  bss_config_t in = {
      .bss_type = kFakeBanjoBssType,
      .remote = kPopulaterBool,
  };
  for (size_t i = 0; i < wlan_ieee80211::kMacAddrLen; i++) {
    in.bssid[i] = kFakeMacAddr[i];
  }

  // Conduct conversion
  wlan_internal::BssConfig out;
  EXPECT_EQ(ZX_OK, ConvertBssConfig(in, &out));

  // Verify outputs
  for (size_t i = 0; i < wlan_ieee80211::kMacAddrLen; i++) {
    EXPECT_EQ(kFakeMacAddr[i], out.bssid.data()[i]);
  }
  EXPECT_EQ(kFakeFidlBssType, out.bss_type);
  EXPECT_EQ(kPopulaterBool, out.remote);

  // Assign an invalid value to cbw, and the conversion will fail.
  in.bss_type = kRandomPopulaterUint32;
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, ConvertBssConfig(in, &out));
}

TEST(ConvertTest, ToFidlBcn) {
  // Populate wlan_bcn_config_t
  uint8_t* tx_packet_template_buffer = (uint8_t*)calloc(kFakePacketSize, sizeof(uint8_t));
  for (size_t i = 0; i < kFakePacketSize; i++) {
    tx_packet_template_buffer[i] = kRandomPopulaterUint8;
  }
  wlan_bcn_config_t in = {
      .packet_template =
          {
              .mac_frame_buffer = tx_packet_template_buffer,
              .mac_frame_size = kFakePacketSize,
              .info =
                  {
                      .tx_flags = kRandomPopulaterUint8,
                      .valid_fields = kRandomPopulaterUint32,
                      .tx_vector_idx = kRandomPopulaterUint16,
                      .phy = kFakeBanjoPhyType,  // Valid PhyType in first try.
                      .channel_bandwidth = kFakeBanjoChannelBandwidth,
                      .mcs = kRandomPopulaterUint8,
                  },
          },
      .tim_ele_offset = kRandomPopulaterUint64,
      .beacon_interval = kRandomPopulaterUint16,
  };

  // Conduct conversion
  fidl::Arena arena;
  wlan_softmac::WlanBcnConfig out;
  ConvertBcn(in, &out, arena);

  // Verify outputs
  EXPECT_EQ(kFakePacketSize, out.packet_template().mac_frame.count());
  for (size_t i = 0; i < kFakePacketSize; i++) {
    EXPECT_EQ(kRandomPopulaterUint8, out.packet_template().mac_frame.data()[i]);
  }

  EXPECT_EQ(kRandomPopulaterUint8, out.packet_template().info.tx_flags);
  EXPECT_EQ(kRandomPopulaterUint32, out.packet_template().info.valid_fields);

  EXPECT_EQ(kRandomPopulaterUint16, out.packet_template().info.tx_vector_idx);

  EXPECT_EQ(kFakeFidlPhyType, out.packet_template().info.phy);

  EXPECT_EQ(kFakeFidlChannelBandwidth, out.packet_template().info.channel_bandwidth);
  EXPECT_EQ(kRandomPopulaterUint8, out.packet_template().info.mcs);
  EXPECT_EQ(kRandomPopulaterUint64, out.tim_ele_offset());
  EXPECT_EQ(kRandomPopulaterUint16, out.beacon_interval());

  // Assign out-of-range values to these fields, and they will be adjust to the default values.
  in.packet_template.info.phy = kRandomPopulaterUint32;
  in.packet_template.info.channel_bandwidth = kRandomPopulaterUint32;
  ConvertBcn(in, &out, arena);
  EXPECT_EQ(wlan_common::WlanPhyType::kDsss, out.packet_template().info.phy);
  EXPECT_EQ(wlan_common::ChannelBandwidth::kCbw20, out.packet_template().info.channel_bandwidth);

  free(tx_packet_template_buffer);
}

TEST(ConvertTest, ToFidlKeyConfig) {
  // Populate wlan_key_config_t
  wlan_key_config_t in = {
      .protection = kFakeBanjoProtection,
      .cipher_type = kRandomPopulaterUint8,
      .key_type = kFakeBanjoKeyType,
      .key_idx = kRandomPopulaterUint8,
      .key_len = wlan_ieee80211::kMaxKeyLen,
      .rsc = kRandomPopulaterUint64,
  };

  for (size_t i = 0; i < wlan_ieee80211::kOuiLen; i++) {
    in.cipher_oui[i] = kFakeOui[i];
  }

  for (size_t i = 0; i < wlan_ieee80211::kMacAddrLen; i++) {
    in.peer_addr[i] = kFakeMacAddr[i];
  }

  for (size_t i = 0; i < wlan_ieee80211::kMaxKeyLen; i++) {
    in.key[i] = kFakeKey[i];
  }

  // Conduct conversion
  fidl::Arena arena;
  wlan_softmac::WlanKeyConfig out;
  EXPECT_EQ(ZX_OK, ConvertKeyConfig(in, &out, arena));

  // Verify outputs
  EXPECT_EQ(kFakeFidlProtection, out.protection());
  EXPECT_EQ(kRandomPopulaterUint8, out.cipher_type());
  EXPECT_EQ(kFakeFidlKeyType, out.key_type());
  EXPECT_EQ(kRandomPopulaterUint8, out.key_idx());
  EXPECT_EQ(kRandomPopulaterUint64, out.rsc());

  for (size_t i = 0; i < wlan_ieee80211::kOuiLen; i++) {
    EXPECT_EQ(kFakeOui[i], out.cipher_oui().data()[i]);
  }

  for (size_t i = 0; i < wlan_ieee80211::kMacAddrLen; i++) {
    EXPECT_EQ(kFakeMacAddr[i], out.peer_addr().data()[i]);
  }

  EXPECT_EQ(wlan_ieee80211::kMaxKeyLen, out.key().count());
  for (size_t i = 0; i < wlan_ieee80211::kMaxKeyLen; i++) {
    EXPECT_EQ(kFakeKey[i], out.key().data()[i]);
  }
}

TEST(ConvertTest, ToFidlPassiveScanArgs) {
  // Populate wlan_softmac_passive_scan_args_t
  uint8_t* channel_list =
      (uint8_t*)calloc(wlan_ieee80211::kMaxUniqueChannelNumbers, sizeof(uint8_t));
  for (size_t i = 0; i < wlan_ieee80211::kMaxUniqueChannelNumbers; i++) {
    channel_list[i] = kFakeChannel;
  }

  wlan_softmac_passive_scan_args_t in = {
      .channels_list = channel_list,
      .channels_count = wlan_ieee80211::kMaxUniqueChannelNumbers,
      .min_channel_time = kFakeDuration,
      .max_channel_time = kFakeDuration,
      .min_home_time = kFakeDuration,
  };

  // Conduct conversion
  fidl::Arena arena;
  wlan_softmac::WlanSoftmacPassiveScanArgs out;
  ConvertPassiveScanArgs(in, &out, arena);

  // Verify outputs
  EXPECT_EQ(wlan_ieee80211::kMaxUniqueChannelNumbers, out.channels().count());
  for (size_t i = 0; i < wlan_ieee80211::kMaxUniqueChannelNumbers; i++) {
    EXPECT_EQ(kFakeChannel, out.channels().data()[i]);
  }
  EXPECT_EQ(kFakeDuration, out.min_channel_time());
  EXPECT_EQ(kFakeDuration, out.max_channel_time());
  EXPECT_EQ(kFakeDuration, out.min_home_time());

  free(channel_list);
}

TEST(ConvertTest, ToFidlActiveScanArgs) {
  // Populate wlan_softmac_active_scan_args_t
  uint8_t* channel_list =
      (uint8_t*)calloc(wlan_ieee80211::kMaxUniqueChannelNumbers, sizeof(uint8_t));
  for (size_t i = 0; i < wlan_ieee80211::kMaxUniqueChannelNumbers; i++) {
    channel_list[i] = kFakeChannel;
  }

  cssid_t* ssid_list = (cssid_t*)calloc(wlan_ieee80211::kSsidListMax, sizeof(cssid_t));
  for (size_t i = 0; i < wlan_ieee80211::kSsidListMax; i++) {
    ssid_list[i].len = kFakeSsidLen;
    memcpy(ssid_list[i].data, kFakeSsid, kFakeSsidLen);
  }

  uint8_t* mac_header =
      (uint8_t*)calloc(wlan_ieee80211::kMaxMgmtFrameMacHeaderByteLen, sizeof(uint8_t));
  for (size_t i = 0; i < wlan_ieee80211::kMaxMgmtFrameMacHeaderByteLen; i++) {
    mac_header[i] = kRandomPopulaterUint8;
  }

  uint8_t* ies = (uint8_t*)calloc(wlan_ieee80211::kMaxVhtMpduByteLen2, sizeof(uint8_t));
  for (size_t i = 0; i < wlan_ieee80211::kMaxVhtMpduByteLen2; i++) {
    ies[i] = kRandomPopulaterUint8;
  }

  wlan_softmac_active_scan_args_t in = {
      .channels_list = channel_list,
      .channels_count = wlan_ieee80211::kMaxUniqueChannelNumbers,
      .ssids_list = ssid_list,
      .ssids_count = wlan_ieee80211::kSsidListMax,
      .mac_header_buffer = mac_header,
      .mac_header_size = wlan_ieee80211::kMaxMgmtFrameMacHeaderByteLen,
      .ies_buffer = ies,
      .ies_size = wlan_ieee80211::kMaxVhtMpduByteLen2,
      .min_channel_time = kFakeDuration,
      .max_channel_time = kFakeDuration,
      .min_home_time = kFakeDuration,
      .min_probes_per_channel = kRandomPopulaterUint8,
      .max_probes_per_channel = kRandomPopulaterUint8,
  };

  // Conduct conversion
  fidl::Arena arena;
  wlan_softmac::WlanSoftmacActiveScanArgs out;
  ConvertActiveScanArgs(in, &out, arena);

  // Verify outputs
  EXPECT_EQ(wlan_ieee80211::kMaxUniqueChannelNumbers, out.channels().count());
  for (size_t i = 0; i < wlan_ieee80211::kMaxUniqueChannelNumbers; i++) {
    EXPECT_EQ(kFakeChannel, out.channels().data()[i]);
  }

  EXPECT_EQ(wlan_ieee80211::kSsidListMax, out.ssids().count());
  for (size_t i = 0; i < wlan_ieee80211::kSsidListMax; i++) {
    auto& ssid = out.ssids();
    EXPECT_EQ(kFakeSsidLen, ssid[i].len);
    EXPECT_EQ(0, memcmp(ssid[i].data.data(), kFakeSsid, ssid[i].len));
  }

  EXPECT_EQ(wlan_ieee80211::kMaxMgmtFrameMacHeaderByteLen, out.mac_header().count());
  for (size_t i = 0; i < wlan_ieee80211::kMaxMgmtFrameMacHeaderByteLen; i++) {
    EXPECT_EQ(kRandomPopulaterUint8, out.mac_header().data()[i]);
  }

  EXPECT_EQ(wlan_ieee80211::kMaxVhtMpduByteLen2, out.ies().count());
  for (size_t i = 0; i < wlan_ieee80211::kMaxVhtMpduByteLen2; i++) {
    EXPECT_EQ(kRandomPopulaterUint8, out.ies().data()[i]);
  }

  EXPECT_EQ(kFakeDuration, out.min_channel_time());
  EXPECT_EQ(kFakeDuration, out.max_channel_time());
  EXPECT_EQ(kFakeDuration, out.min_home_time());
  EXPECT_EQ(kRandomPopulaterUint8, out.min_probes_per_channel());
  EXPECT_EQ(kRandomPopulaterUint8, out.max_probes_per_channel());

  free(channel_list);
  free(ssid_list);
  free(mac_header);
  free(ies);
}

void VerifyWlanWmmAcParams(wlan_associnfo::WlanWmmAcParams& in) {
  EXPECT_EQ(kRandomPopulaterUint8, in.ecw_min);
  EXPECT_EQ(kRandomPopulaterUint8, in.ecw_max);
  EXPECT_EQ(kRandomPopulaterUint8, in.aifsn);
  EXPECT_EQ(kRandomPopulaterUint16, in.txop_limit);
  EXPECT_EQ(kPopulaterBool, in.acm);
}

TEST(ConvertTest, ToFidlAssocCtx) {
  // Populate wlan_assoc_ctx_t
  wlan_assoc_ctx_t in = {
      .aid = kRandomPopulaterUint16,
      .listen_interval = kRandomPopulaterUint16,
      .channel =
          {
              .primary = kFakeChannel,
              .cbw = kFakeBanjoChannelBandwidth,
              .secondary80 = kFakeChannel,
          },
      .qos = kPopulaterBool,
      .wmm_params =
          {
              .apsd = kPopulaterBool,
              .ac_be_params =
                  {
                      .ecw_min = kRandomPopulaterUint8,
                      .ecw_max = kRandomPopulaterUint8,
                      .aifsn = kRandomPopulaterUint8,
                      .txop_limit = kRandomPopulaterUint16,
                      .acm = kPopulaterBool,
                  },
              .ac_bk_params =
                  {
                      .ecw_min = kRandomPopulaterUint8,
                      .ecw_max = kRandomPopulaterUint8,
                      .aifsn = kRandomPopulaterUint8,
                      .txop_limit = kRandomPopulaterUint16,
                      .acm = kPopulaterBool,
                  },
              .ac_vi_params =
                  {
                      .ecw_min = kRandomPopulaterUint8,
                      .ecw_max = kRandomPopulaterUint8,
                      .aifsn = kRandomPopulaterUint8,
                      .txop_limit = kRandomPopulaterUint16,
                      .acm = kPopulaterBool,
                  },
              .ac_vo_params =
                  {
                      .ecw_min = kRandomPopulaterUint8,
                      .ecw_max = kRandomPopulaterUint8,
                      .aifsn = kRandomPopulaterUint8,
                      .txop_limit = kRandomPopulaterUint16,
                      .acm = kPopulaterBool,
                  },
          },
      .rates_cnt = kFakeAssocCtxRateCount,
      .capability_info = kRandomPopulaterUint16,
      .has_ht_cap = kPopulaterBool,
      .has_ht_op = kPopulaterBool,
      .ht_op =
          {
              .primary_channel = kFakeChannel,
              .head = kRandomPopulaterUint32,
              .tail = kRandomPopulaterUint8,
              //  mcs_set initialized below
          },
      .has_vht_cap = kPopulaterBool,
      .has_vht_op = kPopulaterBool,
      .vht_op =
          {
              .vht_cbw = kRandomPopulaterUint8,
              .center_freq_seg0 = kRandomPopulaterUint8,
              .center_freq_seg1 = kRandomPopulaterUint8,
              .basic_mcs = kRandomPopulaterUint16,
          },
  };

  memcpy(in.bssid, kFakeMacAddr, wlan_ieee80211::kMacAddrLen);
  for (size_t i = 0; i < kFakeAssocCtxRateCount; i++) {
    in.rates[i] = kFakeRate;
  }

  for (size_t i = 0; i < wlan_ieee80211::kHtCapLen; i++) {
    in.ht_cap.bytes[i] = kFakeHtCapBytes[i];
  }

  for (size_t i = 0; i < kFakeMcsSetLen; i++) {
    in.ht_op.mcs_set[i] = kRandomPopulaterUint8;
  }

  for (size_t i = 0; i < wlan_ieee80211::kVhtCapLen; i++) {
    in.vht_cap.bytes[i] = kFakeVhtCapBytes[i];
  }

  // Conduct the conversion
  wlan_associnfo::WlanAssocCtx out;
  EXPECT_EQ(ZX_OK, ConvertAssocCtx(in, &out));

  // Verify outputs
  EXPECT_EQ(0, memcmp(out.bssid.data(), kFakeMacAddr, wlan_ieee80211::kMacAddrLen));
  EXPECT_EQ(kRandomPopulaterUint16, out.aid);
  EXPECT_EQ(kRandomPopulaterUint16, out.listen_interval);

  EXPECT_EQ(kFakeChannel, out.channel.primary);
  EXPECT_EQ(kFakeFidlChannelBandwidth, out.channel.cbw);
  EXPECT_EQ(kFakeChannel, out.channel.secondary80);

  EXPECT_EQ(kPopulaterBool, out.qos);
  EXPECT_EQ(kPopulaterBool, out.wmm_params.apsd);
  VerifyWlanWmmAcParams(out.wmm_params.ac_be_params);
  VerifyWlanWmmAcParams(out.wmm_params.ac_bk_params);
  VerifyWlanWmmAcParams(out.wmm_params.ac_vi_params);
  VerifyWlanWmmAcParams(out.wmm_params.ac_vo_params);

  EXPECT_EQ(kFakeAssocCtxRateCount, out.rates_cnt);
  for (size_t i = 0; i < kFakeAssocCtxRateCount; i++) {
    EXPECT_EQ(out.rates.data()[i], kFakeRate);
  }
  EXPECT_EQ(kRandomPopulaterUint16, out.capability_info);

  EXPECT_EQ(kPopulaterBool, out.has_ht_cap);
  EXPECT_EQ(0, memcmp(out.ht_cap.bytes.data(), kFakeHtCapBytes, wlan_ieee80211::kHtCapLen));

  EXPECT_EQ(kPopulaterBool, out.has_ht_op);
  EXPECT_EQ(kFakeChannel, out.ht_op.primary_channel);
  EXPECT_EQ(kRandomPopulaterUint32, out.ht_op.head);
  EXPECT_EQ(kRandomPopulaterUint8, out.ht_op.tail);
  for (size_t i = 0; i < kFakeMcsSetLen; i++) {
    EXPECT_EQ(out.ht_op.mcs_set.data()[i], kRandomPopulaterUint8);
  }

  EXPECT_EQ(kPopulaterBool, out.has_vht_cap);
  EXPECT_EQ(0, memcmp(out.vht_cap.bytes.data(), kFakeVhtCapBytes, wlan_ieee80211::kVhtCapLen));

  EXPECT_EQ(kPopulaterBool, out.has_vht_op);
  EXPECT_EQ(kRandomPopulaterUint8, out.vht_op.vht_cbw);
  EXPECT_EQ(kRandomPopulaterUint8, out.vht_op.center_freq_seg0);
  EXPECT_EQ(kRandomPopulaterUint8, out.vht_op.center_freq_seg1);
  EXPECT_EQ(kRandomPopulaterUint16, out.vht_op.basic_mcs);
}

}  // namespace
}  // namespace wlan
