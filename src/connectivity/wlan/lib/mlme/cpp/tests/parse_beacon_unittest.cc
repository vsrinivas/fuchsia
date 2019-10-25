// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <wlan/common/channel.h>
#include <wlan/common/parse_element.h>
#include <wlan/mlme/parse_beacon.h>
#include <wlan/mlme/wlan.h>

namespace wlan {

namespace wlan_mlme = ::fuchsia::wlan::mlme;

using SR = SupportedRate;

VhtOperation make_vht_op(VhtOperation::VhtChannelBandwidth cbw, uint8_t seg0, uint8_t seg1) {
  VhtOperation vht_op;
  vht_op.vht_cbw = to_enum_type(cbw);
  vht_op.center_freq_seg0 = seg0;
  vht_op.center_freq_seg1 = seg1;
  return vht_op;
}

TEST(ParseBeaconTest, GetVhtCbw) {
  EXPECT_EQ(GetVhtCbw(make_vht_op(VhtOperation::VhtChannelBandwidth::VHT_CBW_80_160_80P80, 0, 5)),
            std::optional<wlan_channel_bandwidth_t>{});
  EXPECT_EQ(GetVhtCbw(make_vht_op(VhtOperation::VhtChannelBandwidth::VHT_CBW_80_160_80P80, 0, 10)),
            std::optional<wlan_channel_bandwidth_t>{});
  EXPECT_EQ(GetVhtCbw(make_vht_op(VhtOperation::VhtChannelBandwidth::VHT_CBW_80_160_80P80, 8, 0)),
            std::optional{WLAN_CHANNEL_BANDWIDTH__80});
  EXPECT_EQ(GetVhtCbw(make_vht_op(VhtOperation::VhtChannelBandwidth::VHT_CBW_80_160_80P80, 0, 8)),
            std::optional{WLAN_CHANNEL_BANDWIDTH__160});
  EXPECT_EQ(GetVhtCbw(make_vht_op(VhtOperation::VhtChannelBandwidth::VHT_CBW_80_160_80P80, 0, 20)),
            std::optional{WLAN_CHANNEL_BANDWIDTH__80P80});
  EXPECT_EQ(GetVhtCbw(make_vht_op(VhtOperation::VhtChannelBandwidth::VHT_CBW_20_40, 0, 8)),
            std::optional<wlan_channel_bandwidth_t>{});
}

TEST(ParseBeaconTest, DeriveChannel) {
  // Fun fact: operator== for wlan_channel_t ignores the 'secondary80' field.

  // No DSSS or HT => use rx channel
  EXPECT_EQ(DeriveChannel(3, {}, nullptr, {}), (wlan_channel_t{3, WLAN_CHANNEL_BANDWIDTH__20, 0}));

  // DSSS wins over rx channel
  EXPECT_EQ(DeriveChannel(3, 4, nullptr, {}), (wlan_channel_t{4, WLAN_CHANNEL_BANDWIDTH__20, 0}));

  // HT wins over DSSS
  {
    HtOperation ht_op;
    ht_op.primary_chan = 36;
    ht_op.head.set_secondary_chan_offset(
        to_enum_type(HtOpInfoHead::SecChanOffset::SECONDARY_BELOW));
    ht_op.head.set_sta_chan_width(to_enum_type(HtOpInfoHead::StaChanWidth::ANY));
    EXPECT_EQ(DeriveChannel(3, 4, &ht_op, {}),
              (wlan_channel_t{36, WLAN_CHANNEL_BANDWIDTH__40BELOW, 0}));
  }

  // sta_chan_width set to TWENTY overrides bandwidth
  {
    HtOperation ht_op;
    ht_op.primary_chan = 36;
    ht_op.head.set_secondary_chan_offset(
        to_enum_type(HtOpInfoHead::SecChanOffset::SECONDARY_BELOW));
    ht_op.head.set_sta_chan_width(to_enum_type(HtOpInfoHead::StaChanWidth::TWENTY));
    EXPECT_EQ(DeriveChannel(3, 4, &ht_op, {}), (wlan_channel_t{36, WLAN_CHANNEL_BANDWIDTH__20, 0}));
  }

  // VHT overrides CBW if HT is present
  {
    HtOperation ht_op;
    ht_op.primary_chan = 36;
    ht_op.head.set_secondary_chan_offset(
        to_enum_type(HtOpInfoHead::SecChanOffset::SECONDARY_BELOW));
    ht_op.head.set_sta_chan_width(to_enum_type(HtOpInfoHead::StaChanWidth::ANY));
    EXPECT_EQ(DeriveChannel(3, 4, &ht_op, {WLAN_CHANNEL_BANDWIDTH__160}),
              (wlan_channel_t{36, WLAN_CHANNEL_BANDWIDTH__160, 0}));
  }
}

TEST(ParseBeaconTest, FillRates) {
  struct TestVector {
    std::vector<SupportedRate> supp_rates;
    std::vector<SupportedRate> ext_supp_rates;
    std::vector<uint8_t> want_rates;
  };

  std::vector<TestVector> tvs{
      // clang-format off
    {{SR{111},},        {},                           {111,} },
    {{},                           {SR{111},},        {111,} },
    {{SR::basic(111),}, {},                           {SR::basic(111)}},
    {{},                           {SR::basic(111),}, {SR::basic(111)}},
    {{SR{97},},         {SR::basic(111),}, {97, SR::basic(111)}},
    {{SR::basic(97),},  {SR{111},},        {SR::basic(97), 111}},
    {{SR::basic(97),},  {SR::basic(111),}, {SR::basic(97), SR::basic(111)}},
      // clang-format on
  };
  for (auto tv : tvs) {
    std::vector<uint8_t> got_rates{};
    FillRates(tv.supp_rates, tv.ext_supp_rates, &got_rates);
    EXPECT_EQ(got_rates, tv.want_rates);
  }
}

TEST(ParseBeaconTest, ParseBeaconElements) {
  const uint8_t ies[] = {
      0,    3,    'f',  'o',  'o',                                 // SSID
      1,    8,    0x81, 0x82, 0x83, 0x84, 0x05, 0x06, 0x07, 0x08,  // Supported Rates
      3,    1,    13,                                              // DSSS Param Set
      7,    3,    'A',  'B',  'C',                                 // Country
      50,   3,    0x09, 0x0a, 0x0b,                                // Ext Supp Rates
      48,   2,    0xaa, 0xbb,                                      // RSN
      45,   26,                                                    // HT Caps
      0xaa, 0xbb,                                                  // ht cap info
      0xff,                                                        // ampdu params
      0x0,  0x1,  0x2,  0x3,  0x4,  0x5,  0x6,  0x7,               // mcs
      0x8,  0x9,  0xa,  0xb,  0xc,  0xd,  0xe,  0xf,               // mcs
      0xdd, 0xee,                                                  // ext caps
      0x11, 0x22, 0x33, 0x44,                                      // beamforming
      0x77,                                                        // asel
      61,   22,                                                    // HT Operation
      36,                                                          // primary channel
      0x11, 0x22, 0x33, 0x44, 0x55,                                // HT Op Info
      0x0,  0x1,  0x2,  0x3,  0x4,  0x5,  0x6,  0x7,               // mcs
      0x8,  0x9,  0xa,  0xb,  0xc,  0xd,  0xe,  0xf,               // mcs
      191,  12,                                                    // Vht Caps id and length
      0xaa, 0xbb, 0xcc, 0xdd,                                      // vht cap
      0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,              // vht cap
      192,  5,                                                     // Vht Operation
      1,    155,  42,   0x33, 0x55,                                // vht op
  };
  wlan_mlme::BSSDescription bss_desc;
  ParseBeaconElements(ies, 40, &bss_desc);

  EXPECT_EQ(bss_desc.ssid, std::vector<uint8_t>({'f', 'o', 'o'}));
  EXPECT_EQ(bss_desc.rates, std::vector<uint8_t>({0x81, 0x82, 0x83, 0x84, 0x05, 0x06, 0x07, 0x08,
                                                  0x09, 0x0a, 0x0b}));
  EXPECT_EQ(bss_desc.chan.primary, 36);
  EXPECT_EQ(*bss_desc.country, std::vector<uint8_t>({'A', 'B', 'C'}));
  EXPECT_EQ(*bss_desc.rsn, std::vector<uint8_t>({48, 2, 0xaa, 0xbb}));
  ASSERT_NE(bss_desc.ht_cap, nullptr);
  EXPECT_EQ(common::ParseHtCapabilities(bss_desc.ht_cap->bytes)->ampdu_params.exponent(), 3);
  ASSERT_NE(bss_desc.ht_op, nullptr);
  EXPECT_EQ(common::ParseHtOperation(bss_desc.ht_op->bytes)->primary_chan, 36);
  ASSERT_NE(bss_desc.vht_cap, nullptr);
  ASSERT_NE(bss_desc.vht_op, nullptr);
}

}  // namespace wlan
