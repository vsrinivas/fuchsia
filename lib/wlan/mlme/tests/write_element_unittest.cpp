// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/common/write_element.h>
#include <wlan/mlme/wlan.h>
#include <gtest/gtest.h>

#include "test_utils.h"

namespace wlan {
namespace common {

struct Buf {
    uint8_t data[128] = {};
    BufferWriter w { data };
};

static bool equal(Span<const uint8_t> a, Span<const uint8_t> b) {
    return std::equal(a.begin(), a.end(), b.begin(), b.end());
}

TEST(WriteElement, Ssid) {
    const uint8_t ssid[] = { 'f', 'o', 'o' };
    const uint8_t expected[] = { 0, 3, 'f', 'o', 'o' };
    Buf buf;
    WriteSsid(&buf.w, ssid);
    EXPECT_TRUE(equal(expected, buf.w.WrittenData()));
}

TEST(WriteElement, SupportedRates) {
    SupportedRate rates[] = { SupportedRate(5, true), SupportedRate(20), SupportedRate(30) };
    const uint8_t expected[] = { 1, 3, 0x85u, 20, 30 };
    Buf buf;
    WriteSupportedRates(&buf.w, rates);
    EXPECT_TRUE(equal(expected, buf.w.WrittenData()));
}

TEST(WriteElement, DsssParamSet) {
    const uint8_t expected[] = { 3, 1, 11 };
    Buf buf;
    WriteDsssParamSet(&buf.w, 11);
    EXPECT_TRUE(equal(expected, buf.w.WrittenData()));
}

TEST(WriteElement, CfParamSet) {
    const uint8_t expected[] = { 4, 6, 10, 20, 0x11, 0x22, 0x33, 0x44 };
    Buf buf;
    WriteCfParamSet(&buf.w, { 10, 20, 0x2211, 0x4433 });
    EXPECT_TRUE(equal(expected, buf.w.WrittenData()));
}

TEST(WriteElement, Tim) {
    const uint8_t expected[] = { 5, 8, 1, 2, 3, 10, 20, 30, 40, 50 };
    Buf buf;

    std::vector<uint8_t> bmp = { 10, 20, 30, 40, 50 };
    TimHeader hdr;
    hdr.dtim_count = 1;
    hdr.dtim_period = 2;
    hdr.bmp_ctrl = BitmapControl { 3u };
    WriteTim(&buf.w, hdr, bmp);
    EXPECT_TRUE(equal(expected, buf.w.WrittenData()));
}

TEST(WriteElement, CountryPadded) {
    const uint8_t expected[] = { 7, 10, 'A', 'B', 'C', 36, 1, 17, 100, 1, 17, 0 };
    Buf buf;
    SubbandTriplet subbands[] = {{ 36, 1, 17 }, { 100, 1, 17 }};
    WriteCountry(&buf.w, {{'A', 'B', 'C'}}, subbands);
    EXPECT_TRUE(equal(expected, buf.w.WrittenData()));
}

TEST(WriteElement, CountryUnpadded) {
    const uint8_t expected[] = { 7, 6, 'A', 'B', 'C', 36, 1, 17 };
    Buf buf;
    SubbandTriplet subbands[] = {{ 36, 1, 17 }};
    WriteCountry(&buf.w, {{'A', 'B', 'C'}}, subbands);
    EXPECT_TRUE(equal(expected, buf.w.WrittenData()));
}

TEST(WriteElement, ExtendedSupportedRates) {
    SupportedRate rates[] = { SupportedRate(10), SupportedRate(20), SupportedRate(30) };
    const uint8_t expected[] = { 50, 3, 10, 20, 30 };
    Buf buf;
    WriteExtendedSupportedRates(&buf.w, rates);
    EXPECT_TRUE(equal(expected, buf.w.WrittenData()));
}

TEST(WriteElement, MeshConfiguration) {
    const uint8_t expected[] = { 113, 7, 1, 1, 0, 1, 1, 0xCC, 0xF0 };
    Buf buf;
    const MeshConfiguration mesh_config = {MeshConfiguration::kHwmp,
                                           MeshConfiguration::kAirtime,
                                           MeshConfiguration::kCongestCtrlInactive,
                                           MeshConfiguration::kNeighborOffsetSync,
                                           MeshConfiguration::kSae,
                                           MeshConfiguration::MeshFormationInfo { 0xCC },
                                           MeshConfiguration::MeshCapability { 0xF0 }};
    WriteMeshConfiguration(&buf.w, mesh_config);
    EXPECT_TRUE(equal(expected, buf.w.WrittenData()));
}

TEST(WriteElement, MeshId) {
    const uint8_t expected[] = { 114, 3, 'f', 'o', 'o' };
    Buf buf;
    const uint8_t mesh_id[] = { 'f', 'o', 'o' };
    WriteMeshId(&buf.w, mesh_id);
    EXPECT_TRUE(equal(expected, buf.w.WrittenData()));
}

TEST(WriteElement, QosCapability) {
    const uint8_t expected[] = { 46, 1, 42 };
    Buf buf;
    WriteQosCapability(&buf.w, QosInfo { 42 });
    EXPECT_TRUE(equal(expected, buf.w.WrittenData()));
}

TEST(WriteElement, GcrGroupAddress) {
    const uint8_t expected[] = { 189, 6, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66 };
    Buf buf;
    common::MacAddr mac_addr { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66 };
    WriteGcrGroupAddress(&buf.w, mac_addr);
    EXPECT_TRUE(equal(expected, buf.w.WrittenData()));
}

TEST(WriteElement, HtCapabilities) {
    const uint8_t expected[] = { 45, 26,
                                 0xaa, 0xbb, // ht cap info
                                 0x55, // ampdu params
                                 0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7,
                                 0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf, // mcs
                                 0xdd, 0xee, // ext caps
                                 0x11, 0x22, 0x33, 0x44, // beamforming
                                 0x77 }; // asel
    Buf buf;
    HtCapabilities ht_caps = {
        .ht_cap_info = HtCapabilityInfo { 0xbbaa },
        .ampdu_params = AmpduParams { 0x55 },
        .mcs_set = SupportedMcsSet {
            .rx_mcs_head = SupportedMcsRxMcsHead { 0x0706050403020100ul },
            .rx_mcs_tail = SupportedMcsRxMcsTail { 0x0b0a0908u },
            .tx_mcs = SupportedMcsTxMcs { 0x0f0e0d0cu },
        },
        .ht_ext_cap = HtExtCapabilities { 0xeeddu },
        .txbf_cap = TxBfCapability { 0x44332211u },
        .asel_cap = AselCapability { 0x77 },
    };
    WriteHtCapabilities(&buf.w, ht_caps);
    EXPECT_TRUE(equal(expected, buf.w.WrittenData()));
}

TEST(WriteElement, HtOperation) {
    const uint8_t expected[] = { 61, 22, 36, 0x11, 0x22, 0x33, 0x44, 0x55,
                                 0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7,
                                 0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf };
    Buf buf;
    HtOperation ht_op = {
        .primary_chan = 36,
        .head = HtOpInfoHead { 0x44332211u },
        .tail = HtOpInfoTail { 0x55 },
        .basic_mcs_set = SupportedMcsSet {
            .rx_mcs_head = SupportedMcsRxMcsHead { 0x0706050403020100ul },
            .rx_mcs_tail = SupportedMcsRxMcsTail { 0x0b0a0908u },
            .tx_mcs = SupportedMcsTxMcs { 0x0f0e0d0cu },
        },
    };
    WriteHtOperation(&buf.w, ht_op);
    EXPECT_TRUE(equal(expected, buf.w.WrittenData()));
}

TEST(WriteElement, VhtCapabilities) {
    const uint8_t expected[] = { 191, 12, 0xaa, 0xbb, 0xcc, 0xdd,
                                 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88 };
    Buf buf;
    VhtCapabilities caps = {
        .vht_cap_info = VhtCapabilitiesInfo { 0xddccbbaau },
        .vht_mcs_nss = VhtMcsNss { 0x8877665544332211ul },
    };
    WriteVhtCapabilities(&buf.w, caps);
    EXPECT_TRUE(equal(expected, buf.w.WrittenData()));
}

TEST(WriteElement, VhtOperation) {
    const uint8_t expected[] = { 192, 5, 1, 155, 42, 0x33, 0x55 };
    Buf buf;
    VhtOperation vhtop = {
        .vht_cbw = VhtOperation::VHT_CBW_80_160_80P80,
        .center_freq_seg0 = 155,
        .center_freq_seg1 = 42,
        .basic_mcs = BasicVhtMcsNss { 0x5533 },
    };
    WriteVhtOperation(&buf.w, vhtop);
    EXPECT_TRUE(equal(expected, buf.w.WrittenData()));
}

TEST(WriteElement, MpmOpenNoPmk) {
    Buf buf;
    WriteMpmOpen(&buf.w, MpmHeader { MpmHeader::AMPE, 0x4433u }, nullptr);

    const uint8_t expected[6] = { 117, 4, 0x01, 0x00, 0x33, 0x44 };
    EXPECT_RANGES_EQ(expected, buf.w.WrittenData());
}

TEST(WriteElement, MpmOpenWithPmk) {
    Buf buf;
    const MpmPmk pmk = {
        .data = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 },
    };
    WriteMpmOpen(&buf.w, MpmHeader { MpmHeader::AMPE, 0x4433u }, &pmk);

    const uint8_t expected[22] = {
        117, 20,
        0x01, 0x00, 0x33, 0x44,
        1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16
    };
    EXPECT_RANGES_EQ(expected, buf.w.WrittenData());
}

TEST(WriteElement, MpmConfirmNoPmk) {
    Buf buf;
    WriteMpmConfirm(&buf.w, MpmHeader { MpmHeader::AMPE, 0x4433u }, 0x6655u, nullptr);

    const uint8_t expected[8] = { 117, 6, 0x01, 0x00, 0x33, 0x44, 0x55, 0x66 };
    EXPECT_RANGES_EQ(expected, buf.w.WrittenData());
}

TEST(WriteElement, MpmConfirmWithPmk) {
    Buf buf;
    const MpmPmk pmk = {
        .data = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 },
    };
    WriteMpmConfirm(&buf.w, MpmHeader { MpmHeader::AMPE, 0x4433u }, 0x6655u, &pmk);

    const uint8_t expected[24] = {
        117, 22,
        0x01, 0x00, 0x33, 0x44, 0x55, 0x66,
        1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16
    };
    EXPECT_RANGES_EQ(expected, buf.w.WrittenData());
}

} // namespace common
} // namespace wlan
