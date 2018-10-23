// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/common/parse_element.h>
#include <gtest/gtest.h>

namespace wlan {
namespace common {

TEST(ParseElement, Ssid) {
    const uint8_t raw_body[] = { 'f', 'o', 'o' };
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
    const uint8_t raw_body[] = { 10, 20, 30, 40, 50, 60, 70, 80 };
    std::optional<Span<const SupportedRate>> rates = ParseSupportedRates(raw_body);
    ASSERT_TRUE(rates);
    EXPECT_EQ(raw_body, reinterpret_cast<const uint8_t*>(rates->data()));
    EXPECT_EQ(8u, rates->size());
}

TEST(ParseElement, SupportedRatesEmpty) {
    std::optional<Span<const SupportedRate>> rates = ParseSupportedRates({});
    ASSERT_FALSE(rates);
}

TEST(ParseElement, SupportedRatesTooLong) {
    const uint8_t raw_body[] = { 10, 20, 30, 40, 50, 60, 70, 80, 90 };
    std::optional<Span<const SupportedRate>> rates = ParseSupportedRates(raw_body);
    ASSERT_FALSE(rates);
}

TEST(ParseElement, DsssParamSet) {
    const uint8_t raw_body[] = { 11 };
    const DsssParamSet* dsss = ParseDsssParamSet(raw_body);
    ASSERT_NE(nullptr, dsss);
    ASSERT_EQ(11u, dsss->current_chan);
}

TEST(ParseElement, DsssParamSetToShort) {
    const DsssParamSet* dsss = ParseDsssParamSet({});
    ASSERT_EQ(nullptr, dsss);
}

TEST(ParseElement, DsssParamSetToLong) {
    const uint8_t raw_body[] = { 11, 12 };
    const DsssParamSet* dsss = ParseDsssParamSet(raw_body);
    ASSERT_EQ(nullptr, dsss);
}

TEST(ParseElement, CfParamSet) {
    const uint8_t raw_body[] = { 1, 2, 3, 4, 5, 6 };
    const CfParamSet* cf = ParseCfParamSet(raw_body);
    ASSERT_NE(nullptr, cf);
    ASSERT_EQ(1, cf->count);
    ASSERT_EQ(2, cf->period);
    ASSERT_EQ(0x0403, cf->max_duration);
    ASSERT_EQ(0x0605, cf->dur_remaining);
}

TEST(ParseElement, CfParamSetTooShort) {
    const uint8_t raw_body[] = { 1, 2, 3, 4, 5 };
    const CfParamSet* cf = ParseCfParamSet(raw_body);
    ASSERT_EQ(nullptr, cf);
}

TEST(ParseElement, CfParamSetTooLong) {
    const uint8_t raw_body[] = { 1, 2, 3, 4, 5, 6, 7 };
    const CfParamSet* cf = ParseCfParamSet(raw_body);
    ASSERT_EQ(nullptr, cf);
}

TEST(ParseElement, Tim) {
    const uint8_t raw_body[] = { 1, 2, 3, 4, 5 };
    std::optional<ParsedTim> tim = ParseTim(raw_body);
    ASSERT_TRUE(tim);
    EXPECT_EQ(1, tim->header.dtim_count);
    EXPECT_EQ(2, tim->header.dtim_period);
    EXPECT_EQ(3, tim->header.bmp_ctrl.val());
    EXPECT_EQ(&raw_body[3], tim->bitmap.data());
    EXPECT_EQ(2u, tim->bitmap.size());
}

TEST(ParseElement, TimEmptyBitmap) {
    const uint8_t raw_body[] = { 1, 2, 3 };
    std::optional<ParsedTim> tim = ParseTim(raw_body);
    ASSERT_FALSE(tim);
}

TEST(ParseElement, TimTooShort) {
    const uint8_t raw_body[] = { 1, 2 };
    std::optional<ParsedTim> tim = ParseTim(raw_body);
    ASSERT_FALSE(tim);
}

TEST(ParseElement, CountryNoTriplets) {
    const uint8_t raw_body[] = { 'A', 'B', 'C', 0 };
    std::optional<ParsedCountry> c = ParseCountry(raw_body);
    ASSERT_TRUE(c);
    EXPECT_EQ('A', c->country.data[0]);
    EXPECT_EQ('B', c->country.data[1]);
    EXPECT_EQ('C', c->country.data[2]);
    EXPECT_TRUE(c->triplets.empty());
}

TEST(ParseElement, CountrySingleTriplet) {
    const uint8_t raw_body[] = { 'A', 'B', 'C', 1, 2, 3 };
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
    const uint8_t raw_body[] = { 'A', 'B', 'C', 1, 2, 3, 4, 5, 6, 0 };
    std::optional<ParsedCountry> c = ParseCountry(raw_body);
    ASSERT_TRUE(c);
    EXPECT_EQ('A', c->country.data[0]);
    EXPECT_EQ('B', c->country.data[1]);
    EXPECT_EQ('C', c->country.data[2]);
    EXPECT_EQ(&raw_body[3], reinterpret_cast<const uint8_t*>(c->triplets.data()));
    EXPECT_EQ(2u, c->triplets.size());
}

TEST(ParseElement, CountryTooShort) {
    const uint8_t raw_body[] = { 'A', 'B' };
    std::optional<ParsedCountry> c = ParseCountry(raw_body);
    ASSERT_FALSE(c);
}

TEST(ParseElement, ExtendedSupportedRates) {
    const uint8_t raw_body[] = { 10, 20, 30, 40, 50, 60, 70, 80, 90 };
    std::optional<Span<const SupportedRate>> rates = ParseExtendedSupportedRates(raw_body);
    ASSERT_TRUE(rates);
    EXPECT_EQ(raw_body, reinterpret_cast<const uint8_t*>(rates->data()));
    EXPECT_EQ(9u, rates->size());
}

TEST(ParseElement, ExtendedSupportedRatesEmpty) {
    std::optional<Span<const SupportedRate>> rates = ParseExtendedSupportedRates({});
    ASSERT_FALSE(rates);
}

TEST(ParseElement, MeshConfiguration) {
    const uint8_t raw_body[] = { 1, 2, 3, 4, 5, 6, 7 };
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
    const uint8_t raw_body[] = { 1, 2, 3, 4, 5, 6 };
    const MeshConfiguration* mc = ParseMeshConfiguration(raw_body);
    ASSERT_EQ(nullptr, mc);
}

TEST(ParseElement, MeshConfigurationTooLong) {
    const uint8_t raw_body[] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    const MeshConfiguration* mc = ParseMeshConfiguration(raw_body);
    ASSERT_EQ(nullptr, mc);
}

TEST(ParseElement, MeshId) {
    const uint8_t raw_body[] = { 'f', 'o', 'o' };
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
    const uint8_t raw_body[] = { 5 };
    const QosInfo* qos = ParseQosCapability(raw_body);
    ASSERT_NE(nullptr, qos);
    EXPECT_EQ(5, qos->val());
}

TEST(ParseElement, QosCapabilityTooShort) {
    const QosInfo* qos = ParseQosCapability({});
    ASSERT_EQ(nullptr, qos);
}

TEST(ParseElement, QosCapabilityTooLong) {
    const uint8_t raw_body[] = { 5, 6 };
    const QosInfo* qos = ParseQosCapability(raw_body);
    ASSERT_EQ(nullptr, qos);
}

TEST(ParseElement, GcrGroupAddress) {
    const uint8_t raw_body[] = { 1, 2, 3, 4, 5, 6 };
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
    const uint8_t raw_body[] = { 1, 2, 3, 4, 5 };
    const common::MacAddr* addr = ParseGcrGroupAddress(raw_body);
    ASSERT_EQ(nullptr, addr);
}

TEST(ParseElement, GcrGroupAddressTooLong) {
    const uint8_t raw_body[] = { 1, 2, 3, 4, 5, 6, 7 };
    const common::MacAddr* addr = ParseGcrGroupAddress(raw_body);
    ASSERT_EQ(nullptr, addr);
}

TEST(ParseElement, HtCapabilities) {
    const uint8_t raw_body[26] = {
        0xaa, 0xbb, // ht cap info
        0x55, // ampdu params
        0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7,
        0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf, // mcs
        0xdd, 0xee, // ext caps
        0x11, 0x22, 0x33, 0x44, // beamforming
        0x77 // asel
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
    const uint8_t raw_body[22] = { 36, 0x11, 0x22, 0x33, 0x44, 0x55,
                                   0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7,
                                   0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf };
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
    const uint8_t raw_body[12] = { 0xaa, 0xbb, 0xcc, 0xdd,
                                   0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88 };
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
    const uint8_t raw_body[5] = { 1, 155, 42, 0x33, 0x55 };
    const VhtOperation* v = ParseVhtOperation(raw_body);
    ASSERT_NE(nullptr, v);
    EXPECT_EQ(1u, v->vht_cbw);
    EXPECT_EQ(155u, v->center_freq_seg0);
    EXPECT_EQ(42u, v->center_freq_seg1);
    EXPECT_EQ(0x5533, v->basic_mcs.val());
}

TEST(ParseElement, VhtOperationTooShort) {
    const uint8_t raw_body[4] = { 1, 155, 42, 0x33 };
    const VhtOperation* v = ParseVhtOperation(raw_body);
    ASSERT_EQ(nullptr, v);
}

TEST(ParseElement, VhtOperationTooLong) {
    const uint8_t raw_body[6] = { 1, 155, 42, 0x33, 0x44, 0x55 };
    const VhtOperation* v = ParseVhtOperation(raw_body);
    ASSERT_EQ(nullptr, v);
}

} // namespace common
} // namespace wlan

