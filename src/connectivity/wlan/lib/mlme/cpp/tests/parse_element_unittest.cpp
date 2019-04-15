// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <wlan/common/parse_element.h>

namespace wlan {
namespace common {

TEST(ParseElement, Ssid) {
  const uint8_t raw_body[] = {'f', 'o', 'o'};
  std::optional<Span<const uint8_t>> ssid = ParseSsid(raw_body);
  ASSERT_TRUE(ssid);
  EXPECT_EQ(raw_body, ssid->data());
  EXPECT_EQ(3u, ssid->size());
}

TEST(ParseElement, SsidTooLong) {
  const uint8_t raw_body[33] = {};
  std::optional<Span<const uint8_t>> ssid = ParseSsid(raw_body);
  ASSERT_FALSE(ssid);
}

TEST(ParseElement, SupportedRates) {
  const uint8_t raw_body[] = {10, 20, 30, 40, 50, 60, 70, 80};
  std::optional<Span<const SupportedRate>> rates =
      ParseSupportedRates(raw_body);
  ASSERT_TRUE(rates);
  EXPECT_EQ(raw_body, reinterpret_cast<const uint8_t*>(rates->data()));
  EXPECT_EQ(8u, rates->size());
}

TEST(ParseElement, SupportedRatesEmpty) {
  std::optional<Span<const SupportedRate>> rates = ParseSupportedRates({});
  ASSERT_FALSE(rates);
}

TEST(ParseElement, SupportedRatesTooLong) {
  const uint8_t raw_body[] = {10, 20, 30, 40, 50, 60, 70, 80, 90};
  std::optional<Span<const SupportedRate>> rates =
      ParseSupportedRates(raw_body);
  ASSERT_FALSE(rates);
}

TEST(ParseElement, DsssParamSet) {
  const uint8_t raw_body[] = {11};
  const DsssParamSet* dsss = ParseDsssParamSet(raw_body);
  ASSERT_NE(nullptr, dsss);
  ASSERT_EQ(11u, dsss->current_chan);
}

TEST(ParseElement, DsssParamSetToShort) {
  const DsssParamSet* dsss = ParseDsssParamSet({});
  ASSERT_EQ(nullptr, dsss);
}

TEST(ParseElement, DsssParamSetToLong) {
  const uint8_t raw_body[] = {11, 12};
  const DsssParamSet* dsss = ParseDsssParamSet(raw_body);
  ASSERT_EQ(nullptr, dsss);
}

TEST(ParseElement, CfParamSet) {
  const uint8_t raw_body[] = {1, 2, 3, 4, 5, 6};
  const CfParamSet* cf = ParseCfParamSet(raw_body);
  ASSERT_NE(nullptr, cf);
  ASSERT_EQ(1, cf->count);
  ASSERT_EQ(2, cf->period);
  ASSERT_EQ(0x0403, cf->max_duration);
  ASSERT_EQ(0x0605, cf->dur_remaining);
}

TEST(ParseElement, CfParamSetTooShort) {
  const uint8_t raw_body[] = {1, 2, 3, 4, 5};
  const CfParamSet* cf = ParseCfParamSet(raw_body);
  ASSERT_EQ(nullptr, cf);
}

TEST(ParseElement, CfParamSetTooLong) {
  const uint8_t raw_body[] = {1, 2, 3, 4, 5, 6, 7};
  const CfParamSet* cf = ParseCfParamSet(raw_body);
  ASSERT_EQ(nullptr, cf);
}

TEST(ParseElement, Tim) {
  const uint8_t raw_body[] = {1, 2, 3, 4, 5};
  std::optional<ParsedTim> tim = ParseTim(raw_body);
  ASSERT_TRUE(tim);
  EXPECT_EQ(1, tim->header.dtim_count);
  EXPECT_EQ(2, tim->header.dtim_period);
  EXPECT_EQ(3, tim->header.bmp_ctrl.val());
  EXPECT_EQ(&raw_body[3], tim->bitmap.data());
  EXPECT_EQ(2u, tim->bitmap.size());
}

TEST(ParseElement, TimEmptyBitmap) {
  const uint8_t raw_body[] = {1, 2, 3};
  std::optional<ParsedTim> tim = ParseTim(raw_body);
  ASSERT_FALSE(tim);
}

TEST(ParseElement, TimTooShort) {
  const uint8_t raw_body[] = {1, 2};
  std::optional<ParsedTim> tim = ParseTim(raw_body);
  ASSERT_FALSE(tim);
}

TEST(ParseElement, CountryNoTriplets) {
  const uint8_t raw_body[] = {'A', 'B', 'C', 0};
  std::optional<ParsedCountry> c = ParseCountry(raw_body);
  ASSERT_TRUE(c);
  EXPECT_EQ('A', c->country.data[0]);
  EXPECT_EQ('B', c->country.data[1]);
  EXPECT_EQ('C', c->country.data[2]);
  EXPECT_TRUE(c->triplets.empty());
}

TEST(ParseElement, CountrySingleTriplet) {
  const uint8_t raw_body[] = {'A', 'B', 'C', 1, 2, 3};
  std::optional<ParsedCountry> c = ParseCountry(raw_body);
  ASSERT_TRUE(c);
  EXPECT_EQ('A', c->country.data[0]);
  EXPECT_EQ('B', c->country.data[1]);
  EXPECT_EQ('C', c->country.data[2]);

  EXPECT_EQ(1u, c->triplets.size());

  EXPECT_EQ(1u, c->triplets[0].first_channel_number);
  EXPECT_EQ(2u, c->triplets[0].number_of_channels);
  EXPECT_EQ(3u, c->triplets[0].max_tx_power);
}

TEST(ParseElement, CountryTwoTriplets) {
  const uint8_t raw_body[] = {'A', 'B', 'C', 1, 2, 3, 4, 5, 6, 0};
  std::optional<ParsedCountry> c = ParseCountry(raw_body);
  ASSERT_TRUE(c);
  EXPECT_EQ('A', c->country.data[0]);
  EXPECT_EQ('B', c->country.data[1]);
  EXPECT_EQ('C', c->country.data[2]);
  EXPECT_EQ(&raw_body[3], reinterpret_cast<const uint8_t*>(c->triplets.data()));
  EXPECT_EQ(2u, c->triplets.size());
}

TEST(ParseElement, CountryTooShort) {
  const uint8_t raw_body[] = {'A', 'B'};
  std::optional<ParsedCountry> c = ParseCountry(raw_body);
  ASSERT_FALSE(c);
}

TEST(ParseElement, ExtendedSupportedRates) {
  const uint8_t raw_body[] = {10, 20, 30, 40, 50, 60, 70, 80, 90};
  std::optional<Span<const SupportedRate>> rates =
      ParseExtendedSupportedRates(raw_body);
  ASSERT_TRUE(rates);
  EXPECT_EQ(raw_body, reinterpret_cast<const uint8_t*>(rates->data()));
  EXPECT_EQ(9u, rates->size());
}

TEST(ParseElement, ExtendedSupportedRatesEmpty) {
  std::optional<Span<const SupportedRate>> rates =
      ParseExtendedSupportedRates({});
  ASSERT_FALSE(rates);
}

TEST(ParseElement, MeshConfiguration) {
  const uint8_t raw_body[] = {1, 2, 3, 4, 5, 6, 7};
  const MeshConfiguration* mc = ParseMeshConfiguration(raw_body);
  ASSERT_NE(nullptr, mc);
  EXPECT_EQ(1u, static_cast<uint8_t>(mc->active_path_sel_proto_id));
  EXPECT_EQ(2u, static_cast<uint8_t>(mc->active_path_sel_metric_id));
  EXPECT_EQ(3u, static_cast<uint8_t>(mc->congest_ctrl_method_id));
  EXPECT_EQ(4u, static_cast<uint8_t>(mc->sync_method_id));
  EXPECT_EQ(5u, static_cast<uint8_t>(mc->auth_proto_id));
  EXPECT_EQ(6u, mc->mesh_formation_info.val());
  EXPECT_EQ(7u, mc->mesh_capability.val());
}

TEST(ParseElement, MeshConfigurationTooShort) {
  const uint8_t raw_body[] = {1, 2, 3, 4, 5, 6};
  const MeshConfiguration* mc = ParseMeshConfiguration(raw_body);
  ASSERT_EQ(nullptr, mc);
}

TEST(ParseElement, MeshConfigurationTooLong) {
  const uint8_t raw_body[] = {1, 2, 3, 4, 5, 6, 7, 8};
  const MeshConfiguration* mc = ParseMeshConfiguration(raw_body);
  ASSERT_EQ(nullptr, mc);
}

TEST(ParseElement, MeshId) {
  const uint8_t raw_body[] = {'f', 'o', 'o'};
  std::optional<Span<const uint8_t>> mesh_id = ParseMeshId(raw_body);
  ASSERT_TRUE(mesh_id);
  EXPECT_EQ(raw_body, mesh_id->data());
  EXPECT_EQ(3u, mesh_id->size());
}

TEST(ParseElement, MeshIdTooLong) {
  const uint8_t raw_body[33] = {};
  std::optional<Span<const uint8_t>> mesh_id = ParseMeshId(raw_body);
  ASSERT_FALSE(mesh_id);
}

TEST(ParseElement, QosCapability) {
  const uint8_t raw_body[] = {5};
  const QosInfo* qos = ParseQosCapability(raw_body);
  ASSERT_NE(nullptr, qos);
  EXPECT_EQ(5, qos->val());
}

TEST(ParseElement, QosCapabilityTooShort) {
  const QosInfo* qos = ParseQosCapability({});
  ASSERT_EQ(nullptr, qos);
}

TEST(ParseElement, QosCapabilityTooLong) {
  const uint8_t raw_body[] = {5, 6};
  const QosInfo* qos = ParseQosCapability(raw_body);
  ASSERT_EQ(nullptr, qos);
}

TEST(ParseElement, GcrGroupAddress) {
  const uint8_t raw_body[] = {1, 2, 3, 4, 5, 6};
  const common::MacAddr* addr = ParseGcrGroupAddress(raw_body);
  ASSERT_NE(nullptr, addr);
  EXPECT_EQ(1, addr->byte[0]);
  EXPECT_EQ(2, addr->byte[1]);
  EXPECT_EQ(3, addr->byte[2]);
  EXPECT_EQ(4, addr->byte[3]);
  EXPECT_EQ(5, addr->byte[4]);
  EXPECT_EQ(6, addr->byte[5]);
}

TEST(ParseElement, GcrGroupAddressTooShort) {
  const uint8_t raw_body[] = {1, 2, 3, 4, 5};
  const common::MacAddr* addr = ParseGcrGroupAddress(raw_body);
  ASSERT_EQ(nullptr, addr);
}

TEST(ParseElement, GcrGroupAddressTooLong) {
  const uint8_t raw_body[] = {1, 2, 3, 4, 5, 6, 7};
  const common::MacAddr* addr = ParseGcrGroupAddress(raw_body);
  ASSERT_EQ(nullptr, addr);
}

TEST(ParseElement, HtCapabilities) {
  const uint8_t raw_body[26] = {
      0xaa, 0xbb,  // ht cap info
      0x55,        // ampdu params
      0x0,  0x1,  0x2,  0x3,  0x4, 0x5, 0x6, 0x7,
      0x8,  0x9,  0xa,  0xb,  0xc, 0xd, 0xe, 0xf,  // mcs
      0xdd, 0xee,                                  // ext caps
      0x11, 0x22, 0x33, 0x44,                      // beamforming
      0x77                                         // asel
  };
  const HtCapabilities* h = ParseHtCapabilities(raw_body);
  ASSERT_NE(nullptr, h);
  EXPECT_EQ(0xbbaau, h->ht_cap_info.val());
  EXPECT_EQ(0x55u, h->ampdu_params.val());
  EXPECT_EQ(0x0706050403020100ul, h->mcs_set.rx_mcs_head.val());
  EXPECT_EQ(0x0b0a0908u, h->mcs_set.rx_mcs_tail.val());
  EXPECT_EQ(0x0f0e0d0cu, h->mcs_set.tx_mcs.val());
  EXPECT_EQ(0xeeddu, h->ht_ext_cap.val());
  EXPECT_EQ(0x44332211u, h->txbf_cap.val());
  EXPECT_EQ(0x77u, h->asel_cap.val());
}

TEST(ParseElement, HtCapabilitiesTooShort) {
  const uint8_t raw_body[25] = {};
  const HtCapabilities* h = ParseHtCapabilities(raw_body);
  ASSERT_EQ(nullptr, h);
}

TEST(ParseElement, HtCapabilitiesTooLong) {
  const uint8_t raw_body[27] = {};
  const HtCapabilities* h = ParseHtCapabilities(raw_body);
  ASSERT_EQ(nullptr, h);
}

TEST(ParseElement, HtOperation) {
  const uint8_t raw_body[22] = {36,  0x11, 0x22, 0x33, 0x44, 0x55, 0x0, 0x1,
                                0x2, 0x3,  0x4,  0x5,  0x6,  0x7,  0x8, 0x9,
                                0xa, 0xb,  0xc,  0xd,  0xe,  0xf};
  const HtOperation* h = ParseHtOperation(raw_body);
  ASSERT_NE(nullptr, h);
  EXPECT_EQ(36, h->primary_chan);
  EXPECT_EQ(0x44332211u, h->head.val());
  EXPECT_EQ(0x55u, h->tail.val());
  EXPECT_EQ(0x0706050403020100ul, h->basic_mcs_set.rx_mcs_head.val());
  EXPECT_EQ(0x0b0a0908u, h->basic_mcs_set.rx_mcs_tail.val());
  EXPECT_EQ(0x0f0e0d0cu, h->basic_mcs_set.tx_mcs.val());
}

TEST(ParseElement, HtOperationTooShort) {
  const uint8_t raw_body[21] = {};
  const HtOperation* h = ParseHtOperation(raw_body);
  ASSERT_EQ(nullptr, h);
}

TEST(ParseElement, HtOperationTooLong) {
  const uint8_t raw_body[23] = {};
  const HtOperation* h = ParseHtOperation(raw_body);
  ASSERT_EQ(nullptr, h);
}

TEST(ParseElement, VhtCapabilities) {
  const uint8_t raw_body[12] = {0xaa, 0xbb, 0xcc, 0xdd, 0x11, 0x22,
                                0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
  const VhtCapabilities* v = ParseVhtCapabilities(raw_body);
  ASSERT_NE(nullptr, v);
  EXPECT_EQ(0xddccbbaau, v->vht_cap_info.val());
  EXPECT_EQ(0x8877665544332211ul, v->vht_mcs_nss.val());
}

TEST(ParseElement, VhtCapabilitiesTooShort) {
  const uint8_t raw_body[11] = {};
  const VhtCapabilities* v = ParseVhtCapabilities(raw_body);
  ASSERT_EQ(nullptr, v);
}

TEST(ParseElement, VhtCapabilitiesTooLong) {
  const uint8_t raw_body[13] = {};
  const VhtCapabilities* v = ParseVhtCapabilities(raw_body);
  ASSERT_EQ(nullptr, v);
}

TEST(ParseElement, VhtOperation) {
  const uint8_t raw_body[5] = {1, 155, 42, 0x33, 0x55};
  const VhtOperation* v = ParseVhtOperation(raw_body);
  ASSERT_NE(nullptr, v);
  EXPECT_EQ(1u, v->vht_cbw);
  EXPECT_EQ(155u, v->center_freq_seg0);
  EXPECT_EQ(42u, v->center_freq_seg1);
  EXPECT_EQ(0x5533, v->basic_mcs.val());
}

TEST(ParseElement, VhtOperationTooShort) {
  const uint8_t raw_body[4] = {1, 155, 42, 0x33};
  const VhtOperation* v = ParseVhtOperation(raw_body);
  ASSERT_EQ(nullptr, v);
}

TEST(ParseElement, VhtOperationTooLong) {
  const uint8_t raw_body[6] = {1, 155, 42, 0x33, 0x44, 0x55};
  const VhtOperation* v = ParseVhtOperation(raw_body);
  ASSERT_EQ(nullptr, v);
}

TEST(ParseElement, MpmOpenBad) {
  {
    const uint8_t too_short[3] = {0x11, 0x22, 0x33};
    EXPECT_FALSE(ParseMpmOpen(too_short));
  }
  {
    const uint8_t weird_length[5] = {0x11, 0x22, 0x33, 0x44, 0x55};
    EXPECT_FALSE(ParseMpmOpen(weird_length));
  }
  {
    const uint8_t too_long[21] = {};
    EXPECT_FALSE(ParseMpmOpen(too_long));
  }
}

TEST(ParseElement, MpmOpenGoodNoPmk) {
  const uint8_t data[] = {0x11, 0x22, 0x33, 0x44};
  auto mpm = ParseMpmOpen(data);
  ASSERT_TRUE(mpm);
  EXPECT_EQ(static_cast<uint16_t>(mpm->header.protocol), 0x2211u);
  EXPECT_EQ(static_cast<uint16_t>(mpm->header.local_link_id), 0x4433u);
  EXPECT_EQ(mpm->pmk, nullptr);
}

TEST(ParseElement, MpmOpenGoodWithPmk) {
  const uint8_t data[20] = {0x11, 0x22, 0x33, 0x44, 1,  2,  3,  4,  5,  6,
                            7,    8,    9,    10,   11, 12, 13, 14, 15, 16};
  auto mpm = ParseMpmOpen(data);
  ASSERT_TRUE(mpm);
  EXPECT_EQ(static_cast<uint16_t>(mpm->header.protocol), 0x2211u);
  EXPECT_EQ(static_cast<uint16_t>(mpm->header.local_link_id), 0x4433u);
  EXPECT_EQ(static_cast<const void*>(mpm->pmk), data + 4);
}

TEST(ParseElement, MpmConfirmBad) {
  {
    const uint8_t too_short[] = {0x11, 0x22, 0x33, 0x44, 0x55};
    EXPECT_FALSE(ParseMpmConfirm(too_short));
  }
  {
    const uint8_t weird_length[] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77};
    EXPECT_FALSE(ParseMpmConfirm(weird_length));
  }
  {
    const uint8_t too_long[23] = {};
    EXPECT_FALSE(ParseMpmConfirm(too_long));
  }
}

TEST(ParseElement, MpmConfirmGoodNoPmk) {
  const uint8_t data[] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
  auto mpm = ParseMpmConfirm(data);
  ASSERT_TRUE(mpm);
  EXPECT_EQ(static_cast<uint16_t>(mpm->header.protocol), 0x2211u);
  EXPECT_EQ(static_cast<uint16_t>(mpm->header.local_link_id), 0x4433u);
  EXPECT_EQ(static_cast<uint16_t>(mpm->peer_link_id), 0x6655u);
  EXPECT_EQ(mpm->pmk, nullptr);
}

TEST(ParseElement, MpmConfirmGoodWithPmk) {
  const uint8_t data[22] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 1, 2,
                            3,    4,    5,    6,    7,    8,    9, 10,
                            11,   12,   13,   14,   15,   16};
  auto mpm = ParseMpmConfirm(data);
  ASSERT_TRUE(mpm);
  EXPECT_EQ(static_cast<uint16_t>(mpm->header.protocol), 0x2211u);
  EXPECT_EQ(static_cast<uint16_t>(mpm->header.local_link_id), 0x4433u);
  EXPECT_EQ(static_cast<uint16_t>(mpm->peer_link_id), 0x6655u);
  EXPECT_EQ(static_cast<const void*>(mpm->pmk), data + 6);
}

TEST(ParseElement, MpmCloseBad) {
  {
    const uint8_t too_short[5] = {0x11, 0x22, 0x33, 0x44, 0x55};
    EXPECT_FALSE(ParseMpmClose(too_short));
  }
  {
    const uint8_t weird_length[7] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77};
    EXPECT_FALSE(ParseMpmClose(weird_length));
  }
  {
    const uint8_t weird_length[9] = {0x11, 0x22, 0x33, 0x44, 0x55,
                                     0x66, 0x77, 0x88, 0x99};
    EXPECT_FALSE(ParseMpmClose(weird_length));
  }
  {
    const uint8_t too_long[25] = {};
    EXPECT_FALSE(ParseMpmClose(too_long));
  }
}

TEST(ParseElement, MpmCloseGoodNoLinkIdNoPmk) {
  const uint8_t data[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
  auto mpm = ParseMpmClose(data);
  ASSERT_TRUE(mpm);
  EXPECT_EQ(static_cast<uint16_t>(mpm->header.protocol), 0x2211u);
  EXPECT_EQ(static_cast<uint16_t>(mpm->header.local_link_id), 0x4433u);
  EXPECT_EQ(mpm->peer_link_id, std::optional<uint16_t>{});
  EXPECT_EQ(mpm->reason_code, 0x6655u);
  EXPECT_EQ(mpm->pmk, nullptr);
}

TEST(ParseElement, MpmCloseGoodWithLinkIdNoPmk) {
  const uint8_t data[8] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
  auto mpm = ParseMpmClose(data);
  ASSERT_TRUE(mpm);
  EXPECT_EQ(static_cast<uint16_t>(mpm->header.protocol), 0x2211u);
  EXPECT_EQ(static_cast<uint16_t>(mpm->header.local_link_id), 0x4433u);
  EXPECT_EQ(mpm->peer_link_id, std::optional<uint16_t>{0x6655u});
  EXPECT_EQ(mpm->reason_code, 0x8877u);
  EXPECT_EQ(mpm->pmk, nullptr);
}

TEST(ParseElement, MpmCloseGoodNoLinkIdWithPmk) {
  const uint8_t data[22] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 1, 2,
                            3,    4,    5,    6,    7,    8,    9, 10,
                            11,   12,   13,   14,   15,   16};
  auto mpm = ParseMpmClose(data);
  ASSERT_TRUE(mpm);
  EXPECT_EQ(static_cast<uint16_t>(mpm->header.protocol), 0x2211u);
  EXPECT_EQ(static_cast<uint16_t>(mpm->header.local_link_id), 0x4433u);
  EXPECT_EQ(mpm->peer_link_id, std::optional<uint16_t>{});
  EXPECT_EQ(mpm->reason_code, 0x6655u);
  EXPECT_EQ(static_cast<const void*>(mpm->pmk), data + 6);
}

TEST(ParseElement, MpmCloseGoodWithLinkIdWithPmk) {
  const uint8_t data[24] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
                            1,    2,    3,    4,    5,    6,    7,    8,
                            9,    10,   11,   12,   13,   14,   15,   16};
  auto mpm = ParseMpmClose(data);
  ASSERT_TRUE(mpm);
  EXPECT_EQ(static_cast<uint16_t>(mpm->header.protocol), 0x2211u);
  EXPECT_EQ(static_cast<uint16_t>(mpm->header.local_link_id), 0x4433u);
  EXPECT_EQ(mpm->peer_link_id, std::optional<uint16_t>{0x6655u});
  EXPECT_EQ(mpm->reason_code, 0x8877u);
  EXPECT_EQ(static_cast<const void*>(mpm->pmk), data + 8);
}

TEST(ParseElement, PreqMinimal) {
  // clang-format off
    const uint8_t data[17 + 9] = {
        0x00, // flags
        0x02, // hop count
        0x03, // element ttl
        0x04, 0x05, 0x06, 0x07, // path discovery ID
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, // originator addr
        0x0e, 0x0f, 0x10, 0x11, // originator hwmp seqno
        0x18, 0x19, 0x1a, 0x1b, // lifetime
        0x1c, 0x1d, 0x1e, 0x1f, // metric
        // Target count. Having no targets probably doesn't make sense,
        // but we test this code path anyway.
        0,
    };
  // clang-format on
  auto preq = ParsePreq(data);
  ASSERT_TRUE(preq);

  EXPECT_EQ(data, reinterpret_cast<const uint8_t*>(preq->header));
  EXPECT_EQ(0x02u, preq->header->hop_count);

  EXPECT_EQ(nullptr, preq->originator_external_addr);
  EXPECT_EQ(0x1b1a1918u, preq->middle->lifetime);
  EXPECT_EQ(0u, preq->per_target.size());
}

TEST(ParseElement, PreqFull) {
  // clang-format off
    const uint8_t data[17 + 9 + 6 + 2*11] = {
        0x40, // flags: address extension = true
        0x02, // hop count
        0x03, // element ttl
        0x04, 0x05, 0x06, 0x07, // path discovery ID
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, // originator addr
        0x0e, 0x0f, 0x10, 0x11, // originator hwmp seqno
        0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, // originator external address
        0x18, 0x19, 0x1a, 0x1b, // lifetime
        0x1c, 0x1d, 0x1e, 0x1f, // metric
        2, // target count
        // Target 1
        0x00, // target flags
        0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, // target address
        0xa1, 0xa2, 0xa3, 0xa4, // target hwmp seqno
        // Target 2
        0x00, // target flags
        0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, // target address
        0xb1, 0xb2, 0xb3, 0xb4, // target hwmp seqno
    };
  // clang-format on
  auto preq = ParsePreq(data);
  ASSERT_TRUE(preq);

  EXPECT_EQ(data, reinterpret_cast<const uint8_t*>(preq->header));
  EXPECT_EQ(0x02u, preq->header->hop_count);

  ASSERT_NE(nullptr, preq->originator_external_addr);
  EXPECT_EQ(MacAddr("16:17:18:19:1a:1b"), *preq->originator_external_addr);

  EXPECT_EQ(0x1b1a1918u, preq->middle->lifetime);

  ASSERT_EQ(2u, preq->per_target.size());
  ASSERT_EQ(MacAddr("bb:bb:bb:bb:bb:bb"), preq->per_target[1].target_addr);
}

TEST(ParseElement, PreqTooLong) {
  // clang-format off
    const uint8_t data[17 + 9 + 1] = {
        0x00, // flags
        0x02, // hop count
        0x03, // element ttl
        0x04, 0x05, 0x06, 0x07, // path discovery ID
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, // originator addr
        0x0e, 0x0f, 0x10, 0x11, // originator hwmp seqno
        0x18, 0x19, 0x1a, 0x1b, // lifetime
        0x1c, 0x1d, 0x1e, 0x1f, // metric
        0, // target count
        1 // extra byte
    };
  // clang-format on
  auto preq = ParsePreq(data);
  ASSERT_FALSE(preq);
}

TEST(ParseElement, PreqTooShort_Header) {
  // clang-format off
    const uint8_t data[17 - 1] = {
        0x00, // flags
        0x02, // hop count
        0x03, // element ttl
        0x04, 0x05, 0x06, 0x07, // path discovery ID
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, // originator addr
        0x0e, 0x0f, 0x10, // one byte missing from originator hwmp seqno
    };
  // clang-format on
  auto preq = ParsePreq(data);
  ASSERT_FALSE(preq);
}

TEST(ParseElement, PreqTooShort_OrigExtAddr) {
  // clang-format off
    const uint8_t data[17 + 6 - 1] = {
        0x40, // flags: address extension = true
        0x02, // hop count
        0x03, // element ttl
        0x04, 0x05, 0x06, 0x07, // path discovery ID
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, // originator addr
        0x0e, 0x0f, 0x10, 0x11, // originator hwmp seqno
        0x16, 0x17, 0x18, 0x19, 0x1a, // one byte missing from originator external address
    };
  // clang-format on
  auto preq = ParsePreq(data);
  ASSERT_FALSE(preq);
}

TEST(ParseElement, PreqTooShort_Middle) {
  // clang-format off
    const uint8_t data[17 + 9 - 1] = {
        0x00, // flags
        0x02, // hop count
        0x03, // element ttl
        0x04, 0x05, 0x06, 0x07, // path discovery ID
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, // originator addr
        0x0e, 0x0f, 0x10, 0x11, // originator hwmp seqno
        0x18, 0x19, 0x1a, 0x1b, // lifetime
        0x1c, 0x1d, 0x1e, 0x1f, // metric
        // Target count missing
    };
  // clang-format on
  auto preq = ParsePreq(data);
  ASSERT_FALSE(preq);
}

TEST(ParseElement, PreqTooShort_PerTarget) {
  // clang-format off
    const uint8_t data[26 + 6 + 2*11 - 1] = {
        0x40, // flags: address extension = true
        0x02, // hop count
        0x03, // element ttl
        0x04, 0x05, 0x06, 0x07, // path discovery ID
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, // originator addr
        0x0e, 0x0f, 0x10, 0x11, // originator hwmp seqno
        0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, // originator external address
        0x18, 0x19, 0x1a, 0x1b, // lifetime
        0x1c, 0x1d, 0x1e, 0x1f, // metric
        2, // target count
        // Target 1
        0x00, // target flags
        0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, // target address
        0xa1, 0xa2, 0xa3, 0xa4, // target hwmp seqno
        // Target 2
        0x00, // target flags
        0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, // target address
        0xb1, 0xb2, 0xb3, // one byte missing from target hwmp seqno
    };
  // clang-format on
  auto preq = ParsePreq(data);
  ASSERT_FALSE(preq);
}

TEST(ParseElement, PrepNoExtAddr) {
  // clang-format off
    const uint8_t data[] = {
        0x00, 0x01, 0x02, // flags, hop count, elem ttl
        0x03, 0x04, 0x05, 0x06, 0x07, 0x08, // target addr
        0x09, 0x0a, 0x0b, 0x0c, // target hwmp seqno
        0x0d, 0x0e, 0x0f, 0x10, // lifetime
        0x11, 0x12, 0x13, 0x14, // metric
        0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, // originator addr
        0x1b, 0x1c, 0x1d, 0x1e, // originator hwmp seqno
    };
  // clang-format on
  auto prep = ParsePrep(data);
  ASSERT_TRUE(prep);

  EXPECT_EQ(data, reinterpret_cast<const uint8_t*>(prep->header));
  EXPECT_EQ(0x01u, prep->header->hop_count);

  EXPECT_EQ(nullptr, prep->target_external_addr);

  EXPECT_EQ(MacAddr("15:16:17:18:19:1a"), prep->tail->originator_addr);
}

TEST(ParseElement, PrepWithExtAddr) {
  // clang-format off
    const uint8_t data[] = {
        0x40, 0x01, 0x02, // flags, hop count, elem ttl
        0x03, 0x04, 0x05, 0x06, 0x07, 0x08, // target addr
        0x09, 0x0a, 0x0b, 0x0c, // target hwmp seqno
        0x44, 0x55, 0x66, 0x77, 0x88, 0x99, // target external addr
        0x0d, 0x0e, 0x0f, 0x10, // lifetime
        0x11, 0x12, 0x13, 0x14, // metric
        0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, // originator addr
        0x1b, 0x1c, 0x1d, 0x1e, // originator hwmp seqno
    };
  // clang-format on
  auto prep = ParsePrep(data);
  ASSERT_TRUE(prep);

  EXPECT_EQ(data, reinterpret_cast<const uint8_t*>(prep->header));
  EXPECT_EQ(0x01u, prep->header->hop_count);

  ASSERT_NE(nullptr, prep->target_external_addr);
  EXPECT_EQ(common::MacAddr("44:55:66:77:88:99"), *prep->target_external_addr);

  EXPECT_EQ(MacAddr("15:16:17:18:19:1a"), prep->tail->originator_addr);
}

TEST(ParseElement, PrepTooShort_Header) {
  // clang-format off
    const uint8_t data[] = {
        0x00, 0x01, 0x02, // flags, hop count, elem ttl
        0x03, 0x04, 0x05, 0x06, 0x07, 0x08, // target addr
        0x09, 0x0a, 0x0b, // one byte missing from target hwmp seqno
    };
  // clang-format on
  auto prep = ParsePrep(data);
  ASSERT_FALSE(prep);
}

TEST(ParseElement, PrepTooShort_Tail) {
  // clang-format off
    const uint8_t data[] = {
        0x00, 0x01, 0x02, // flags, hop count, elem ttl
        0x03, 0x04, 0x05, 0x06, 0x07, 0x08, // target addr
        0x09, 0x0a, 0x0b, 0x0c, // target hwmp seqno
        0x0d, 0x0e, 0x0f, 0x10, // lifetime
        0x11, 0x12, 0x13, 0x14, // metric
        0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, // originator addr
        0x1b, 0x1c, 0x1d, // one byte missing from originator hwmp seqno
    };
  // clang-format on
  auto prep = ParsePrep(data);
  ASSERT_FALSE(prep);
}

TEST(ParseElement, PrepTooShort_ExtAddr) {
  // clang-format off
    const uint8_t data[] = {
        0x40, 0x01, 0x02, // flags, hop count, elem ttl
        0x03, 0x04, 0x05, 0x06, 0x07, 0x08, // target addr
        0x09, 0x0a, 0x0b, 0x0c, // target hwmp seqno
        0x44, 0x55, 0x66, 0x77, 0x88, // one byte missing from target external addr
    };
  // clang-format on
  auto prep = ParsePrep(data);
  ASSERT_FALSE(prep);
}

TEST(ParseElement, PrepTooLong) {
  // clang-format off
    const uint8_t data[] = {
        0x00, 0x01, 0x02, // flags, hop count, elem ttl
        0x03, 0x04, 0x05, 0x06, 0x07, 0x08, // target addr
        0x09, 0x0a, 0x0b, 0x0c, // target hwmp seqno
        0x0d, 0x0e, 0x0f, 0x10, // lifetime
        0x11, 0x12, 0x13, 0x14, // metric
        0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, // originator addr
        0x1b, 0x1c, 0x1d, 0x1e, // originator hwmp seqno
        0, // extra byte
    };
  // clang-format on
  auto prep = ParsePrep(data);
  ASSERT_FALSE(prep);
}

TEST(ParseElement, Perr) {
  // clang-format off
    const uint8_t data[] = {
        11, // TTL
        7, // number of destinations
        10, 20, 30, // destination data (not parsed, returned as raw bytes)
    };
  // clang-format on
  auto perr = ParsePerr(data);
  ASSERT_TRUE(perr.has_value());
  EXPECT_EQ(11u, perr->header->element_ttl);
  EXPECT_EQ(7u, perr->header->num_destinations);
  ASSERT_EQ(3u, perr->destinations.size());
  EXPECT_EQ(20, perr->destinations[1]);
}

TEST(ParseElement, PerrNoDestinationData) {
  // clang-format off
    const uint8_t data[] = {
        11, // TTL
        7, // number of destinations
    };
  // clang-format on
  auto perr = ParsePerr(data);
  // Still expect to be parsed correctly. It is the responsibility of
  // PerrDestinationParser to validate the destination data separately.
  ASSERT_TRUE(perr.has_value());
  EXPECT_EQ(11u, perr->header->element_ttl);
  EXPECT_EQ(7u, perr->header->num_destinations);
  ASSERT_EQ(0u, perr->destinations.size());
}

TEST(ParseElement, PerrTooShort) {
  // clang-format off
    const uint8_t data[] = {
        11, // TTL
        // no number of destinations
    };
  // clang-format on
  auto perr = ParsePerr(data);
  ASSERT_FALSE(perr.has_value());
}

}  // namespace common
}  // namespace wlan
