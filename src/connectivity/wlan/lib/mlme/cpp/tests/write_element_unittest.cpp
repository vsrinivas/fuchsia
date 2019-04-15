// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <wlan/common/write_element.h>
#include <wlan/mlme/wlan.h>

#include "test_utils.h"

namespace wlan {
namespace common {

struct Buf {
  uint8_t data[128] = {};
  BufferWriter w{data};
};

static bool equal(Span<const uint8_t> a, Span<const uint8_t> b) {
  return std::equal(a.begin(), a.end(), b.begin(), b.end());
}

TEST(WriteElement, Ssid) {
  const uint8_t ssid[] = {'f', 'o', 'o'};
  const uint8_t expected[] = {0, 3, 'f', 'o', 'o'};
  Buf buf;
  WriteSsid(&buf.w, ssid);
  EXPECT_TRUE(equal(expected, buf.w.WrittenData()));
}

TEST(WriteElement, SupportedRates) {
  SupportedRate rates[] = {SupportedRate(5, true), SupportedRate(20),
                           SupportedRate(30)};
  const uint8_t expected[] = {1, 3, 0x85u, 20, 30};
  Buf buf;
  WriteSupportedRates(&buf.w, rates);
  EXPECT_TRUE(equal(expected, buf.w.WrittenData()));
}

TEST(WriteElement, DsssParamSet) {
  const uint8_t expected[] = {3, 1, 11};
  Buf buf;
  WriteDsssParamSet(&buf.w, 11);
  EXPECT_TRUE(equal(expected, buf.w.WrittenData()));
}

TEST(WriteElement, CfParamSet) {
  const uint8_t expected[] = {4, 6, 10, 20, 0x11, 0x22, 0x33, 0x44};
  Buf buf;
  WriteCfParamSet(&buf.w, {10, 20, 0x2211, 0x4433});
  EXPECT_TRUE(equal(expected, buf.w.WrittenData()));
}

TEST(WriteElement, Tim) {
  const uint8_t expected[] = {5, 8, 1, 2, 3, 10, 20, 30, 40, 50};
  Buf buf;

  std::vector<uint8_t> bmp = {10, 20, 30, 40, 50};
  TimHeader hdr;
  hdr.dtim_count = 1;
  hdr.dtim_period = 2;
  hdr.bmp_ctrl = BitmapControl{3u};
  WriteTim(&buf.w, hdr, bmp);
  EXPECT_TRUE(equal(expected, buf.w.WrittenData()));
}

TEST(WriteElement, CountryPadded) {
  const uint8_t expected[] = {7, 10, 'A', 'B', 'C', 36, 1, 17, 100, 1, 17, 0};
  Buf buf;
  SubbandTriplet subbands[] = {{36, 1, 17}, {100, 1, 17}};
  WriteCountry(&buf.w, {{'A', 'B', 'C'}}, subbands);
  EXPECT_TRUE(equal(expected, buf.w.WrittenData()));
}

TEST(WriteElement, CountryUnpadded) {
  const uint8_t expected[] = {7, 6, 'A', 'B', 'C', 36, 1, 17};
  Buf buf;
  SubbandTriplet subbands[] = {{36, 1, 17}};
  WriteCountry(&buf.w, {{'A', 'B', 'C'}}, subbands);
  EXPECT_TRUE(equal(expected, buf.w.WrittenData()));
}

TEST(WriteElement, ExtendedSupportedRates) {
  SupportedRate rates[] = {SupportedRate(10), SupportedRate(20),
                           SupportedRate(30)};
  const uint8_t expected[] = {50, 3, 10, 20, 30};
  Buf buf;
  WriteExtendedSupportedRates(&buf.w, rates);
  EXPECT_TRUE(equal(expected, buf.w.WrittenData()));
}

TEST(WriteElement, MeshConfiguration) {
  const uint8_t expected[] = {113, 7, 1, 1, 0, 1, 1, 0xCC, 0xF0};
  Buf buf;
  const MeshConfiguration mesh_config = {
      MeshConfiguration::kHwmp,
      MeshConfiguration::kAirtime,
      MeshConfiguration::kCongestCtrlInactive,
      MeshConfiguration::kNeighborOffsetSync,
      MeshConfiguration::kSae,
      MeshConfiguration::MeshFormationInfo{0xCC},
      MeshConfiguration::MeshCapability{0xF0}};
  WriteMeshConfiguration(&buf.w, mesh_config);
  EXPECT_TRUE(equal(expected, buf.w.WrittenData()));
}

TEST(WriteElement, MeshId) {
  const uint8_t expected[] = {114, 3, 'f', 'o', 'o'};
  Buf buf;
  const uint8_t mesh_id[] = {'f', 'o', 'o'};
  WriteMeshId(&buf.w, mesh_id);
  EXPECT_TRUE(equal(expected, buf.w.WrittenData()));
}

TEST(WriteElement, QosCapability) {
  const uint8_t expected[] = {46, 1, 42};
  Buf buf;
  WriteQosCapability(&buf.w, QosInfo{42});
  EXPECT_TRUE(equal(expected, buf.w.WrittenData()));
}

TEST(WriteElement, GcrGroupAddress) {
  const uint8_t expected[] = {189, 6, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
  Buf buf;
  common::MacAddr mac_addr{0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
  WriteGcrGroupAddress(&buf.w, mac_addr);
  EXPECT_TRUE(equal(expected, buf.w.WrittenData()));
}

TEST(WriteElement, HtCapabilities) {
  // clang-format off
    const uint8_t expected[] = { 45, 26,
                                 0xaa, 0xbb, // ht cap info
                                 0x55, // ampdu params
                                 0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7,
                                 0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf, // mcs
                                 0xdd, 0xee, // ext caps
                                 0x11, 0x22, 0x33, 0x44, // beamforming
                                 0x77 }; // asel
  // clang-format on

  Buf buf;
  HtCapabilities ht_caps = {
      .ht_cap_info = HtCapabilityInfo{0xbbaa},
      .ampdu_params = AmpduParams{0x55},
      .mcs_set =
          SupportedMcsSet{
              .rx_mcs_head = SupportedMcsRxMcsHead{0x0706050403020100ul},
              .rx_mcs_tail = SupportedMcsRxMcsTail{0x0b0a0908u},
              .tx_mcs = SupportedMcsTxMcs{0x0f0e0d0cu},
          },
      .ht_ext_cap = HtExtCapabilities{0xeeddu},
      .txbf_cap = TxBfCapability{0x44332211u},
      .asel_cap = AselCapability{0x77},
  };
  WriteHtCapabilities(&buf.w, ht_caps);
  EXPECT_TRUE(equal(expected, buf.w.WrittenData()));
}

TEST(WriteElement, HtOperation) {
  // clang-format off
    const uint8_t expected[] = { 61, 22, 36, 0x11, 0x22, 0x33, 0x44, 0x55,
                                 0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7,
                                 0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf };
  // clang-format on
  Buf buf;
  HtOperation ht_op = {
      .primary_chan = 36,
      .head = HtOpInfoHead{0x44332211u},
      .tail = HtOpInfoTail{0x55},
      .basic_mcs_set =
          SupportedMcsSet{
              .rx_mcs_head = SupportedMcsRxMcsHead{0x0706050403020100ul},
              .rx_mcs_tail = SupportedMcsRxMcsTail{0x0b0a0908u},
              .tx_mcs = SupportedMcsTxMcs{0x0f0e0d0cu},
          },
  };
  WriteHtOperation(&buf.w, ht_op);
  EXPECT_TRUE(equal(expected, buf.w.WrittenData()));
}

TEST(WriteElement, VhtCapabilities) {
  // clang-format off
    const uint8_t expected[] = { 191, 12, 0xaa, 0xbb, 0xcc, 0xdd,
                                 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88 };
  // clang-format on
  Buf buf;
  VhtCapabilities caps = {
      .vht_cap_info = VhtCapabilitiesInfo{0xddccbbaau},
      .vht_mcs_nss = VhtMcsNss{0x8877665544332211ul},
  };
  WriteVhtCapabilities(&buf.w, caps);
  EXPECT_TRUE(equal(expected, buf.w.WrittenData()));
}

TEST(WriteElement, VhtOperation) {
  const uint8_t expected[] = {192, 5, 1, 155, 42, 0x33, 0x55};
  Buf buf;
  VhtOperation vhtop = {
      .vht_cbw = VhtOperation::VHT_CBW_80_160_80P80,
      .center_freq_seg0 = 155,
      .center_freq_seg1 = 42,
      .basic_mcs = BasicVhtMcsNss{0x5533},
  };
  WriteVhtOperation(&buf.w, vhtop);
  EXPECT_TRUE(equal(expected, buf.w.WrittenData()));
}

TEST(WriteElement, MpmOpenNoPmk) {
  Buf buf;
  WriteMpmOpen(&buf.w, MpmHeader{MpmHeader::AMPE, 0x4433u}, nullptr);

  const uint8_t expected[6] = {117, 4, 0x01, 0x00, 0x33, 0x44};
  EXPECT_RANGES_EQ(expected, buf.w.WrittenData());
}

TEST(WriteElement, MpmOpenWithPmk) {
  Buf buf;
  const MpmPmk pmk = {
      .data = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16},
  };
  WriteMpmOpen(&buf.w, MpmHeader{MpmHeader::AMPE, 0x4433u}, &pmk);

  // clang-format off
    const uint8_t expected[22] = {
        117, 20,
        0x01, 0x00, 0x33, 0x44,
        1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16
    };
  // clang-format on
  EXPECT_RANGES_EQ(expected, buf.w.WrittenData());
}

TEST(WriteElement, MpmConfirmNoPmk) {
  Buf buf;
  WriteMpmConfirm(&buf.w, MpmHeader{MpmHeader::AMPE, 0x4433u}, 0x6655u,
                  nullptr);

  const uint8_t expected[8] = {117, 6, 0x01, 0x00, 0x33, 0x44, 0x55, 0x66};
  EXPECT_RANGES_EQ(expected, buf.w.WrittenData());
}

TEST(WriteElement, MpmConfirmWithPmk) {
  Buf buf;
  const MpmPmk pmk = {
      .data = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16},
  };
  WriteMpmConfirm(&buf.w, MpmHeader{MpmHeader::AMPE, 0x4433u}, 0x6655u, &pmk);

  // clang-format off
    const uint8_t expected[24] = {
        117, 22,
        0x01, 0x00, 0x33, 0x44, 0x55, 0x66,
        1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16
    };
  // clang-format on
  EXPECT_RANGES_EQ(expected, buf.w.WrittenData());
}

TEST(WriteElement, PreqMinimal) {
  Buf buf;
  WritePreq(&buf.w,
            PreqHeader{
                .flags = {},
                .hop_count = 0x01,
                .element_ttl = 0x02,
                .path_discovery_id = 0x06050403u,
                .originator_addr = MacAddr("07:08:09:0a:0b:0c"),
                .originator_hwmp_seqno = 0x100f0e0du,
            },
            nullptr,
            PreqMiddle{
                .lifetime = 0x1a191817u,
                .metric = 0x1e1d1c1bu,
                .target_count = 0x0,
            },
            {});
  // clang-format off
    const uint8_t expected[] = {
        130, 17 + 9,
        0x00, // flags
        0x01, // hop count
        0x02, // element ttl
        0x03, 0x04, 0x05, 0x06, // path discovery ID
        0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, // originator addr
        0x0d, 0x0e, 0x0f, 0x10, // originator hwmp seqno
        0x17, 0x18, 0x19, 0x1a, // lifetime
        0x1b, 0x1c, 0x1d, 0x1e, // metric
        // Target count
        0,
    };
  // clang-format on

  EXPECT_RANGES_EQ(expected, buf.w.WrittenData());
}

TEST(WriteElement, PreqFull) {
  PreqFlags flags;
  flags.set_addr_ext(true);
  MacAddr ext_addr("11:12:13:14:15:16");
  PreqPerTarget per_target = {
      .flags = {},
      .target_addr = MacAddr("21:22:23:24:25:26"),
      .target_hwmp_seqno = 0x2a292827u,
  };
  Buf buf;
  WritePreq(&buf.w,
            PreqHeader{
                .flags = flags,
                .hop_count = 0x01,
                .element_ttl = 0x02,
                .path_discovery_id = 0x06050403u,
                .originator_addr = MacAddr("07:08:09:0a:0b:0c"),
                .originator_hwmp_seqno = 0x100f0e0du,
            },
            &ext_addr,
            PreqMiddle{
                .lifetime = 0x1a191817u,
                .metric = 0x1e1d1c1bu,
                .target_count = 0x1,
            },
            {&per_target, 1});
  // clang-format off
    const uint8_t expected[] = {
        130, 17 + 6 + 9 + 11,
        0x40, // flags: ext addr present
        0x01, // hop count
        0x02, // element ttl
        0x03, 0x04, 0x05, 0x06, // path discovery ID
        0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, // originator addr
        0x0d, 0x0e, 0x0f, 0x10, // originator hwmp seqno
        0x11, 0x12, 0x13, 0x14, 0x15, 0x16, // ext addr
        0x17, 0x18, 0x19, 0x1a, // lifetime
        0x1b, 0x1c, 0x1d, 0x1e, // metric
        // Target count
        1,
        0x00, // target 1 flags
        0x21, 0x22, 0x23, 0x24, 0x25, 0x26, // target 1 address
        0x27, 0x28, 0x29, 0x2a, // target 1 hwmp seqno
    };
  // clang-format on
  EXPECT_RANGES_EQ(expected, buf.w.WrittenData());
}

TEST(WriteElement, PrepNoExtAddr) {
  Buf buf;
  const PrepHeader header = {
      .flags = PrepFlags(0x00),
      .hop_count = 0x01,
      .element_ttl = 0x02,
      .target_addr = MacAddr("03:04:05:06:07:08"),
      .target_hwmp_seqno = 0x0c0b0a09u,
  };
  const PrepTail tail = {
      .lifetime = 0x100f0e0du,
      .metric = 0x14131211u,
      .originator_addr = MacAddr("15:16:17:18:19:1a"),
      .originator_hwmp_seqno = 0x1e1d1c1bu,
  };
  WritePrep(&buf.w, header, nullptr, tail);

  // clang-format off
    const uint8_t expected[33] = {
        131, 31,
        0x00, 0x01, 0x02, // flags, hop count, elem ttl
        0x03, 0x04, 0x05, 0x06, 0x07, 0x08, // target addr
        0x09, 0x0a, 0x0b, 0x0c, // target hwmp seqno
        0x0d, 0x0e, 0x0f, 0x10, // lifetime
        0x11, 0x12, 0x13, 0x14, // metric
        0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, // originator addr
        0x1b, 0x1c, 0x1d, 0x1e, // originator hwmp seqno
    };
  // clang-format on
  EXPECT_RANGES_EQ(expected, buf.w.WrittenData());
}

TEST(WriteElement, PrepWithExtAddr) {
  Buf buf;
  const PrepHeader header = {
      .flags = PrepFlags(0x00),
      .hop_count = 0x01,
      .element_ttl = 0x02,
      .target_addr = MacAddr("03:04:05:06:07:08"),
      .target_hwmp_seqno = 0x0c0b0a09u,
  };
  common::MacAddr ext_addr("44:55:66:77:88:99");
  const PrepTail tail = {
      .lifetime = 0x100f0e0du,
      .metric = 0x14131211u,
      .originator_addr = MacAddr("15:16:17:18:19:1a"),
      .originator_hwmp_seqno = 0x1e1d1c1bu,
  };
  WritePrep(&buf.w, header, &ext_addr, tail);

  // clang-format off
    const uint8_t expected[39] = {
        131, 37,
        0x00, 0x01, 0x02, // flags, hop count, elem ttl
        0x03, 0x04, 0x05, 0x06, 0x07, 0x08, // target addr
        0x09, 0x0a, 0x0b, 0x0c, // target hwmp seqno
        0x44, 0x55, 0x66, 0x77, 0x88, 0x99, // target external addr
        0x0d, 0x0e, 0x0f, 0x10, // lifetime
        0x11, 0x12, 0x13, 0x14, // metric
        0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, // originator addr
        0x1b, 0x1c, 0x1d, 0x1e, // originator hwmp seqno
    };
  // clang-format on
  EXPECT_RANGES_EQ(expected, buf.w.WrittenData());
}

TEST(WriteElement, Perr) {
  uint8_t destinations[] = {3, 4};
  Buf buf;
  WritePerr(&buf.w,
            {
                .element_ttl = 1,
                .num_destinations = 2,
            },
            destinations);

  const uint8_t expected[] = {132, 4, 1, 2, 3, 4};
  EXPECT_RANGES_EQ(expected, buf.w.WrittenData());
}

}  // namespace common
}  // namespace wlan
