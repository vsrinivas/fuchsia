// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/mlme/mac_frame.h>
#include <wlan/mlme/wlan.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <utility>

namespace wlan {
namespace {

TEST(ElementReader, IsValid) {
    uint8_t no_len_buf[] = {0};
    ElementReader r1(no_len_buf, sizeof(no_len_buf));
    EXPECT_FALSE(r1.is_valid());

    // clang-format off
    uint8_t bad_len_buf[] = {0, 1,};
    // clang-format on
    ElementReader r2(bad_len_buf, sizeof(bad_len_buf));
    EXPECT_FALSE(r2.is_valid());

    // clang-format off
    uint8_t good_len_buf[] = {0, 2, 3, 4,};
    // clang-format on
    ElementReader r3(good_len_buf, sizeof(good_len_buf));
    EXPECT_TRUE(r3.is_valid());
    EXPECT_EQ(0u, r3.offset());
}

TEST(ElementReader, SkipHeader) {
    // clang-format off
    uint8_t buf[] = {0, 1, 0xa5, 1, 2, 0xa6, 0xa7,};
    // clang-format on
    ElementReader r(buf, sizeof(buf));
    ASSERT_TRUE(r.is_valid());
    ASSERT_EQ(0u, r.offset());

    const ElementHeader* hdr = r.peek();
    ASSERT_NE(nullptr, hdr);
    EXPECT_EQ(0u, hdr->id);
    EXPECT_EQ(1u, hdr->len);

    r.skip(*hdr);
    EXPECT_TRUE(r.is_valid());
    EXPECT_EQ(3u, r.offset());

    hdr = r.peek();
    ASSERT_NE(nullptr, hdr);
    EXPECT_EQ(1u, hdr->id);
    EXPECT_EQ(2u, hdr->len);

    r.skip(*hdr);
    EXPECT_FALSE(r.is_valid());
    EXPECT_EQ(sizeof(buf), r.offset());
}

TEST(ElementReader, ReadElements) {
    // clang-format off
    uint8_t buf[] = {
        // SSID
        0x00, 0x04, 't', 'e', 's', 't',
        // DSSS Parameter Set
        0x03, 0x01, 11,
        // Unknown
        0xdd, 0x05, 0x01, 0x02, 0x03, 0x04, 0x05,
        // Country
        0x07, 0x03, 'U', 'S', 0x00,
    };
    // clang-format on
    ElementReader r(buf, sizeof(buf));
    ASSERT_TRUE(r.is_valid());

    const ElementHeader* hdr = r.peek();
    ASSERT_NE(nullptr, hdr);
    ASSERT_EQ(element_id::kSsid, hdr->id);
    auto ssid_elem = r.read<SsidElement>();
    ASSERT_NE(nullptr, ssid_elem);
    EXPECT_EQ(0, std::memcmp(ssid_elem->ssid, buf + 2, 4));

    ASSERT_TRUE(r.is_valid());
    EXPECT_EQ(6u, r.offset());
    hdr = r.peek();
    ASSERT_NE(nullptr, hdr);
    ASSERT_EQ(element_id::kDsssParamSet, hdr->id);
    auto dsss_elem = r.read<DsssParamSetElement>();
    ASSERT_NE(nullptr, dsss_elem);
    EXPECT_EQ(11, dsss_elem->current_chan);

    ASSERT_TRUE(r.is_valid());
    EXPECT_EQ(9u, r.offset());
    hdr = r.peek();
    ASSERT_NE(nullptr, hdr);
    EXPECT_EQ(element_id::kVendorSpecific, hdr->id);
    r.skip(*hdr);

    ASSERT_TRUE(r.is_valid());
    EXPECT_EQ(16u, r.offset());
    hdr = r.peek();
    ASSERT_NE(nullptr, hdr);
    ASSERT_EQ(element_id::kCountry, hdr->id);
    auto country_elem = r.read<CountryElement>();
    ASSERT_NE(nullptr, country_elem);
    EXPECT_EQ(0, std::memcmp(country_elem->country, buf + 18, 3));

    EXPECT_FALSE(r.is_valid());
    EXPECT_EQ(sizeof(buf), r.offset());
}

TEST(ElementReader, ReadElements_fail) {
    // clang-format off
    uint8_t buf[] = {
        // Country, but too small
        0x07, 0x02, 'U', 'S',
    };
    // clang-format on
    ElementReader r(buf, sizeof(buf));
    // This is valid, because the element length fits within the buffer.
    EXPECT_TRUE(r.is_valid());
    // But we can't read a CountryElement out of it, because the element is too short for that.
    EXPECT_EQ(nullptr, r.read<CountryElement>());
    EXPECT_EQ(0u, r.offset());
}

TEST(ElementWriter, Insert) {
    uint8_t buf[1024] = {};
    ElementWriter w(buf, sizeof(buf));
    EXPECT_EQ(0u, w.size());

    const uint8_t ssid[] = {'t', 'e', 's', 't'};
    EXPECT_TRUE(w.write<SsidElement>(ssid, sizeof(ssid)));
    EXPECT_EQ(6u, w.size());

    std::vector<SupportedRate> rates = {SupportedRate(1), SupportedRate(2), SupportedRate(3),
                                        SupportedRate(4)};
    EXPECT_TRUE(w.write<SupportedRatesElement>(std::move(rates)));
    EXPECT_EQ(12u, w.size());

    EXPECT_TRUE(w.write<DsssParamSetElement>(11));
    EXPECT_EQ(15u, w.size());
}

class Elements : public ::testing::Test {
   protected:
    Elements() { buf_offset_ = buf_; }

    template <typename T> void add_to_buf(const T& value) {
        memcpy(buf_offset_, &value, sizeof(value));
        buf_offset_ += sizeof(value);
    }
    uint8_t* buf_offset_;
    uint8_t buf_[1024] = {};
    size_t actual_ = 0;
};

TEST_F(Elements, Ssid) {
    const uint8_t ssid[] = {'t', 'e', 's', 't', ' ', 's', 's', 'i', 'd'};
    EXPECT_TRUE(SsidElement::Create(buf_, sizeof(buf_), &actual_, ssid, sizeof(ssid)));
    EXPECT_EQ(sizeof(SsidElement) + sizeof(ssid), actual_);

    auto element = FromBytes<SsidElement>(buf_, sizeof(buf_));
    ASSERT_NE(nullptr, element);
    EXPECT_EQ(0, std::memcmp(ssid, element->ssid, sizeof(ssid)));
}

TEST_F(Elements, SsidTooLong) {
    uint8_t ssid[33];
    std::fill_n(ssid, sizeof(ssid), 'x');
    ASSERT_GT(sizeof(ssid), SsidElement::kMaxLen);
    EXPECT_FALSE(SsidElement::Create(buf_, sizeof(buf_), &actual_, ssid, sizeof(ssid)));
}

TEST_F(Elements, SupportedRates) {
    std::vector<SupportedRate> rates = {SupportedRate(1), SupportedRate(2), SupportedRate(3)};
    EXPECT_TRUE(SupportedRatesElement::Create(buf_, sizeof(buf_), &actual_, rates));
    EXPECT_EQ(sizeof(SupportedRatesElement) + rates.size(), actual_);

    // Check that the rates are the same. mismatch will return a pair of iterators pointing to
    // mismatching elements, or the end() iterator.
    auto element = FromBytes<SupportedRatesElement>(buf_, sizeof(buf_));
    ASSERT_NE(nullptr, element);
    auto pair = std::mismatch(rates.begin(), rates.end(), element->rates);
    EXPECT_EQ(rates.end(), pair.first);
}

TEST_F(Elements, SupportedRatesTooLong) {
    std::vector<SupportedRate> rates = {SupportedRate(1), SupportedRate(2), SupportedRate(3),
                                        SupportedRate(4), SupportedRate(5), SupportedRate(6),
                                        SupportedRate(7), SupportedRate(8), SupportedRate(9)};
    ASSERT_GT(rates.size(), SupportedRatesElement::kMaxLen);
    EXPECT_FALSE(SupportedRatesElement::Create(buf_, sizeof(buf_), &actual_, rates));
}

TEST_F(Elements, DsssParamSet) {
    EXPECT_TRUE(DsssParamSetElement::Create(buf_, sizeof(buf_), &actual_, 11));
    EXPECT_EQ(sizeof(DsssParamSetElement), actual_);

    auto element = FromBytes<DsssParamSetElement>(buf_, sizeof(buf_));
    ASSERT_NE(nullptr, element);
    EXPECT_EQ(11u, element->current_chan);
}

TEST_F(Elements, CfParamSet) {
    EXPECT_TRUE(CfParamSetElement::Create(buf_, sizeof(buf_), &actual_, 1, 2, 3, 4));
    EXPECT_EQ(sizeof(CfParamSetElement), actual_);

    auto element = FromBytes<CfParamSetElement>(buf_, sizeof(buf_));
    ASSERT_NE(nullptr, element);
    EXPECT_EQ(1u, element->count);
    EXPECT_EQ(2u, element->period);
    EXPECT_EQ(3u, element->max_duration);
    EXPECT_EQ(4u, element->dur_remaining);
}

TEST_F(Elements, Tim) {
    std::vector<uint8_t> bmp = {1, 2, 3, 4, 5};
    BitmapControl bmp_ctrl = BitmapControl();
    bmp_ctrl.set_group_traffic_ind(1);
    bmp_ctrl.set_offset(7);
    EXPECT_TRUE(
        TimElement::Create(buf_, sizeof(buf_), &actual_, 1, 2, bmp_ctrl, bmp.data(), bmp.size()));
    EXPECT_EQ(sizeof(TimElement) + bmp.size(), actual_);

    auto element = FromBytes<TimElement>(buf_, sizeof(buf_));
    ASSERT_NE(nullptr, element);
    EXPECT_EQ(1u, element->dtim_count);
    EXPECT_EQ(2u, element->dtim_period);
    EXPECT_EQ(1, element->bmp_ctrl.group_traffic_ind());
    EXPECT_EQ(7, element->bmp_ctrl.offset());
    auto pair = std::mismatch(bmp.begin(), bmp.end(), element->bmp);
    EXPECT_EQ(bmp.end(), pair.first);
}

TEST_F(Elements, TimBufferedTraffic) {
    // Set traffic for aids
    std::vector<uint16_t> aids = {1, 42, 1337, 1338, 2007};
    std::vector<uint8_t> bmp(251, 0);
    for (auto const& aid : aids) {
        bmp[aid / 8] |= 1 << (aid % 8);
    }

    BitmapControl bmp_ctrl = BitmapControl();
    bmp_ctrl.set_group_traffic_ind(0);
    bmp_ctrl.set_offset(0);
    EXPECT_TRUE(
        TimElement::Create(buf_, sizeof(buf_), &actual_, 1, 2, bmp_ctrl, bmp.data(), bmp.size()));
    EXPECT_EQ(sizeof(TimElement) + bmp.size(), actual_);

    auto element = FromBytes<TimElement>(buf_, sizeof(buf_));
    ASSERT_NE(nullptr, element);
    for (auto const& aid : aids) {
        EXPECT_EQ(true, element->traffic_buffered(aid));
    }
}

TEST_F(Elements, TimPartialBitmapBufferedTraffic) {
    // Set traffic for aids
    std::vector<uint8_t> bmp(8, 0);  // Include traffic for 64 aids
    bmp[0] |= 1;                     // aid = 32
    bmp[2] |= 1 << 7;                // aid = 55
    bmp[7] |= 1 << 7;                // aid = 95

    BitmapControl bmp_ctrl = BitmapControl();
    bmp_ctrl.set_group_traffic_ind(0);
    bmp_ctrl.set_offset(2);  // Skip first 32 aids
    EXPECT_TRUE(
        TimElement::Create(buf_, sizeof(buf_), &actual_, 1, 2, bmp_ctrl, bmp.data(), bmp.size()));

    auto element = FromBytes<TimElement>(buf_, sizeof(buf_));
    ASSERT_NE(nullptr, element);
    EXPECT_EQ(true, element->traffic_buffered(32));
    EXPECT_EQ(true, element->traffic_buffered(55));
    EXPECT_EQ(true, element->traffic_buffered(95));

    EXPECT_EQ(false, element->traffic_buffered(31));
    EXPECT_EQ(false, element->traffic_buffered(54));
    EXPECT_EQ(false, element->traffic_buffered(56));
    EXPECT_EQ(false, element->traffic_buffered(96));
}

TEST_F(Elements, Country) {
    // TODO(porce): Read from dot11CountryString. The AP mode should start with its country
    const uint8_t kCountry[3] = {'U', 'S', ' '};

    std::vector<SubbandTriplet> subbands;
    // TODO(porce): Read from the AP's regulatory domain
    subbands.push_back({36, 1, 17});
    subbands.push_back({100, 1, 17});

    SubbandTriplet s3 = {149, 1, 23};
    subbands.push_back(s3);

    EXPECT_TRUE(CountryElement::Create(buf_, sizeof(buf_), &actual_, kCountry, subbands));
    size_t len_expected = sizeof(CountryElement) + subbands.size() * sizeof(SubbandTriplet);
    if (len_expected % 2 == 1) {
        len_expected += 1;  // padding
    }

    EXPECT_EQ(len_expected, actual_);

    auto element = FromBytes<CountryElement>(buf_, sizeof(buf_));
    ASSERT_NE(nullptr, element);
    EXPECT_EQ(0, std::memcmp(element->country, kCountry, sizeof(element->country)));
    EXPECT_EQ(0, std::memcmp(element->triplets, subbands.data(), sizeof(SubbandTriplet)));
    EXPECT_EQ(0, std::memcmp(element->triplets + 2 * sizeof(SubbandTriplet), &s3,
                             sizeof(SubbandTriplet)));
}

TEST_F(Elements, MeshConfiguration) {
    MeshConfiguration::MeshFormationInfo formation_info;
    formation_info.set_val(0xCC);
    MeshConfiguration::MeshCapability mesh_cap;
    mesh_cap.set_val(0xF0);

    const MeshConfiguration mesh_config = {
        MeshConfiguration::kHwmp,
        MeshConfiguration::kAirtime,
        MeshConfiguration::kCongestCtrlInactive,
        MeshConfiguration::kNeighborOffsetSync,
        MeshConfiguration::kSae,
        formation_info,
        mesh_cap
    };

    EXPECT_TRUE(MeshConfigurationElement::Create(buf_, sizeof(buf_), &actual_, mesh_config));
    EXPECT_EQ(sizeof(MeshConfigurationElement), actual_);

    auto element = FromBytes<MeshConfigurationElement>(buf_, actual_);
    ASSERT_NE(nullptr, element);
    EXPECT_EQ(0, std::memcmp(&mesh_config, &element->body, sizeof(mesh_config)));
}

TEST_F(Elements, MeshId) {
    const uint8_t mesh_id[] = {'t', 'e', 's', 't', ' ', 'm', 'e', 's', 'h'};
    EXPECT_TRUE(MeshIdElement::Create(buf_, sizeof(buf_), &actual_, mesh_id, sizeof(mesh_id)));
    EXPECT_EQ(sizeof(MeshIdElement) + sizeof(mesh_id), actual_);

    auto element = FromBytes<MeshIdElement>(buf_, actual_);
    ASSERT_NE(nullptr, element);
    EXPECT_EQ(0, std::memcmp(mesh_id, element->mesh_id, sizeof(mesh_id)));
}

TEST_F(Elements, QosAp) {
    QosInfo info;
    info.set_edca_param_set_update_count(9);
    info.set_txop_request(1);
    EXPECT_TRUE(QosCapabilityElement::Create(buf_, sizeof(buf_), &actual_, info));
    EXPECT_EQ(sizeof(QosCapabilityElement), actual_);

    auto element = FromBytes<QosCapabilityElement>(buf_, sizeof(buf_));
    ASSERT_NE(nullptr, element);
    EXPECT_EQ(element->qos_info.val(), info.val());
}

TEST_F(Elements, QosClient) {
    QosInfo info;
    info.set_ac_vo_uapsd_flag(1);
    info.set_ac_vi_uapsd_flag(1);
    info.set_ac_be_uapsd_flag(1);
    info.set_qack(1);
    info.set_more_data_ack(1);
    EXPECT_TRUE(QosCapabilityElement::Create(buf_, sizeof(buf_), &actual_, info));
    EXPECT_EQ(sizeof(QosCapabilityElement), actual_);

    auto element = FromBytes<QosCapabilityElement>(buf_, sizeof(buf_));
    ASSERT_NE(nullptr, element);
    EXPECT_EQ(element->qos_info.val(), info.val());
}

TEST_F(Elements, Tspec) {
    // Values are chosen randomly.
    constexpr uint8_t ts_info[3] = {97, 54, 13};
    constexpr uint16_t nominal_msdu_size = 1068;
    constexpr uint16_t max_msdu_size = 17223;
    constexpr uint32_t min_svc_interval = 3463625064;
    constexpr uint32_t max_svc_interval = 1348743544;
    constexpr uint32_t inactivity_interval = 3254177988;
    constexpr uint32_t suspension_interval = 3114872601;
    constexpr uint32_t svc_start_time = 1977490251;
    constexpr uint32_t min_data_rate = 2288957164;
    constexpr uint32_t mean_data_rate = 3691476893;
    constexpr uint32_t peak_data_rate = 3115603983;
    constexpr uint32_t burst_size = 2196032537;
    constexpr uint32_t delay_bound = 4120916503;
    constexpr uint32_t min_phy_rate = 4071757759;
    constexpr uint32_t surplus_bw_allowance = 12936;
    constexpr uint32_t medium_time = 2196;

    add_to_buf(static_cast<uint8_t>(146));
    add_to_buf(static_cast<uint8_t>(55));
    add_to_buf(ts_info);
    add_to_buf<uint16_t>(nominal_msdu_size);
    add_to_buf<uint16_t>(max_msdu_size);
    add_to_buf<uint32_t>(min_svc_interval);
    add_to_buf<uint32_t>(max_svc_interval);
    add_to_buf<uint32_t>(inactivity_interval);
    add_to_buf<uint32_t>(suspension_interval);
    add_to_buf<uint32_t>(svc_start_time);
    add_to_buf<uint32_t>(min_data_rate);
    add_to_buf<uint32_t>(mean_data_rate);
    add_to_buf<uint32_t>(peak_data_rate);
    add_to_buf<uint32_t>(burst_size);
    add_to_buf<uint32_t>(delay_bound);
    add_to_buf<uint32_t>(min_phy_rate);
    add_to_buf<uint16_t>(surplus_bw_allowance);
    add_to_buf<uint16_t>(medium_time);

    auto element = FromBytes<TspecElement>(buf_, sizeof(buf_));
    ASSERT_NE(nullptr, element);
    EXPECT_EQ(element->nominal_msdu_size.size(), nominal_msdu_size);
    EXPECT_EQ(element->nominal_msdu_size.fixed(), 0);
    EXPECT_EQ(element->max_msdu_size, max_msdu_size);
    EXPECT_EQ(element->min_service_interval, min_svc_interval);
    EXPECT_EQ(element->max_service_interval, max_svc_interval);
    EXPECT_EQ(element->inactivity_interval, inactivity_interval);
    EXPECT_EQ(element->suspension_interval, suspension_interval);
    EXPECT_EQ(element->service_start_time, svc_start_time);
    EXPECT_EQ(element->min_data_rate, min_data_rate);
    EXPECT_EQ(element->mean_data_rate, mean_data_rate);
    EXPECT_EQ(element->peak_data_rate, peak_data_rate);
    EXPECT_EQ(element->burst_size, burst_size);
    EXPECT_EQ(element->delay_bound, delay_bound);
    EXPECT_EQ(element->min_phy_rate, min_phy_rate);
    EXPECT_EQ(element->surplus_bw_allowance, surplus_bw_allowance);
    EXPECT_EQ(element->medium_time, medium_time);
}

TEST_F(Elements, TsInfoAggregation) {
    TsInfo ts_info;
    ts_info.p1.set_access_policy(TsAccessPolicy::kHccaSpca);
    EXPECT_TRUE(ts_info.IsValidAggregation());
    EXPECT_TRUE(ts_info.IsScheduleReserved());

    ts_info.p1.set_access_policy(TsAccessPolicy::kEdca);
    EXPECT_FALSE(ts_info.IsValidAggregation());
    EXPECT_FALSE(ts_info.IsScheduleReserved());

    ts_info.p2.set_schedule(1);
    EXPECT_TRUE(ts_info.IsValidAggregation());
}

TEST_F(Elements, TsInfoScheduleSetting) {
    TsInfo ts_info;
    EXPECT_EQ(ts_info.GetScheduleSetting(), TsScheduleSetting::kNoSchedule);

    ts_info.p1.set_apsd(1);
    EXPECT_EQ(ts_info.GetScheduleSetting(), TsScheduleSetting::kUnschedledApsd);

    ts_info.p1.set_apsd(0);
    ts_info.p2.set_schedule(1);
    EXPECT_EQ(ts_info.GetScheduleSetting(), TsScheduleSetting::kScheduledPsmp_GcrSp);

    ts_info.p1.set_apsd(1);
    EXPECT_EQ(ts_info.GetScheduleSetting(), TsScheduleSetting::kScheduledApsd);
}

TEST_F(Elements, VhtCapabilities) {
    // Consider endianness if test vector changes.
    VhtCapabilitiesInfo vht_cap_info(0xffffffff);
    VhtMcsNss vht_mcs_nss(0xaaaaaaaaaaaaaaaa);

    EXPECT_TRUE(VhtCapabilities::Create(buf_, sizeof(buf_), &actual_, vht_cap_info, vht_mcs_nss));
    EXPECT_EQ(sizeof(VhtCapabilities), actual_);

    auto element = FromBytes<VhtCapabilities>(buf_, sizeof(buf_));
    ASSERT_NE(nullptr, element);

    EXPECT_EQ(element->vht_cap_info.max_mpdu_len(), 3);
    EXPECT_EQ(element->vht_cap_info.supported_cbw_set(), 3);
    EXPECT_EQ(element->vht_cap_info.rx_ldpc(), 1);
    EXPECT_EQ(element->vht_cap_info.sgi_cbw80(), 1);
    EXPECT_EQ(element->vht_cap_info.sgi_cbw160(), 1);
    EXPECT_EQ(element->vht_cap_info.tx_stbc(), 1);

    EXPECT_EQ(element->vht_cap_info.bfee_sts(), 7);
    EXPECT_EQ(element->vht_cap_info.num_sounding(), 7);
    EXPECT_EQ(element->vht_cap_info.link_adapt(), VhtCapabilitiesInfo::LINK_ADAPT_BOTH);

    EXPECT_EQ(element->vht_mcs_nss.rx_max_mcs_ss1(), VhtMcsNss::VHT_MCS_0_TO_9);
    EXPECT_EQ(element->vht_mcs_nss.rx_max_mcs_ss2(), VhtMcsNss::VHT_MCS_0_TO_9);
    EXPECT_EQ(element->vht_mcs_nss.tx_max_mcs_ss4(), VhtMcsNss::VHT_MCS_0_TO_9);
    EXPECT_EQ(element->vht_mcs_nss.max_nsts(), 5);
    EXPECT_EQ(element->vht_mcs_nss.ext_nss_bw(), 1);
}

TEST_F(Elements, VhtOperation) {
    BasicVhtMcsNss basic_mcs(0xaaaa);
    uint8_t vht_cbw = VhtOperation::VHT_CBW_80_160_80P80;
    uint8_t center_freq_seg0 = 155;
    uint8_t center_freq_seg1 = 42;

    EXPECT_TRUE(VhtOperation::Create(buf_, sizeof(buf_), &actual_, vht_cbw, center_freq_seg0,
                                     center_freq_seg1, basic_mcs));
    EXPECT_EQ(sizeof(VhtOperation), actual_);

    auto element = FromBytes<VhtOperation>(buf_, sizeof(buf_));
    ASSERT_NE(nullptr, element);

    EXPECT_EQ(element->vht_cbw, vht_cbw);
    EXPECT_EQ(element->center_freq_seg0, center_freq_seg0);
    EXPECT_EQ(element->center_freq_seg1, center_freq_seg1);

    EXPECT_EQ(element->basic_mcs.ss1(), VhtMcsNss::VHT_MCS_0_TO_9);
    EXPECT_EQ(element->basic_mcs.ss2(), VhtMcsNss::VHT_MCS_0_TO_9);
    EXPECT_EQ(element->basic_mcs.ss8(), VhtMcsNss::VHT_MCS_0_TO_9);
}

TEST(HtCapabilities, DdkConversion) {
    wlan_ht_caps_t ddk{
        .ht_capability_info = 0x016e,
        .ampdu_params = 0x17,
        .mcs_set.rx_mcs_head = 0x00000001000000ff,
        .mcs_set.rx_mcs_tail = 0x01000000,
        .mcs_set.tx_mcs = 0x00000000,
        .ht_ext_capabilities = 0x1234,
        .tx_beamforming_capabilities = 0x12345678,
        .asel_capabilities = 0xff,
    };

    auto ieee = HtCapabilities::FromDdk(ddk);
    EXPECT_EQ(0x016eU, ieee.ht_cap_info.val());
    EXPECT_EQ(0x17U, ieee.ampdu_params.val());
    EXPECT_EQ(0x00000001000000ffU, ieee.mcs_set.rx_mcs_head.val());
    EXPECT_EQ(0x01000000U, ieee.mcs_set.rx_mcs_tail.val());
    EXPECT_EQ(0x00000000U, ieee.mcs_set.tx_mcs.val());
    EXPECT_EQ(0x1234U, ieee.ht_ext_cap.val());
    EXPECT_EQ(0x12345678U, ieee.txbf_cap.val());
    EXPECT_EQ(0xffU, ieee.asel_cap.val());

    auto ddk2 = ieee.ToDdk();
    EXPECT_EQ(ddk.ht_capability_info, ddk2.ht_capability_info);
    EXPECT_EQ(ddk.ampdu_params, ddk2.ampdu_params);
    EXPECT_EQ(ddk.mcs_set.rx_mcs_head, ddk2.mcs_set.rx_mcs_head);
    EXPECT_EQ(ddk.mcs_set.rx_mcs_tail, ddk2.mcs_set.rx_mcs_tail);
    EXPECT_EQ(ddk.mcs_set.tx_mcs, ddk2.mcs_set.tx_mcs);
    EXPECT_EQ(ddk.ht_ext_capabilities, ddk2.ht_ext_capabilities);
    EXPECT_EQ(ddk.tx_beamforming_capabilities, ddk2.tx_beamforming_capabilities);
    EXPECT_EQ(ddk.asel_capabilities, ddk2.asel_capabilities);
}

TEST(HtOperation, DdkConversion) {
    wlan_ht_op ddk{
        .primary_chan = 123,
        .head = 0x01020304,
        .tail = 0x05,
        .basic_mcs_set.rx_mcs_head = 0x00000001000000ff,
        .basic_mcs_set.rx_mcs_tail = 0x01000000,
        .basic_mcs_set.tx_mcs = 0x00000000,
    };

    auto ieee = HtOperation::FromDdk(ddk);
    EXPECT_EQ(123U, ieee.primary_chan);
    EXPECT_EQ(0x01020304U, ieee.head.val());
    EXPECT_EQ(0x05U, ieee.tail.val());
    EXPECT_EQ(0x00000001000000ffU, ieee.basic_mcs_set.rx_mcs_head.val());
    EXPECT_EQ(0x01000000U, ieee.basic_mcs_set.rx_mcs_tail.val());
    EXPECT_EQ(0x00000000U, ieee.basic_mcs_set.tx_mcs.val());

    auto ddk2 = ieee.ToDdk();
    EXPECT_EQ(ddk.primary_chan, ddk2.primary_chan);
    EXPECT_EQ(ddk.head, ddk2.head);
    EXPECT_EQ(ddk.tail, ddk2.tail);
    EXPECT_EQ(ddk.basic_mcs_set.rx_mcs_head, ddk2.basic_mcs_set.rx_mcs_head);
    EXPECT_EQ(ddk.basic_mcs_set.rx_mcs_tail, ddk2.basic_mcs_set.rx_mcs_tail);
    EXPECT_EQ(ddk.basic_mcs_set.tx_mcs, ddk2.basic_mcs_set.tx_mcs);
}

TEST(VhtCapabilities, DdkConversion) {
    wlan_vht_caps ddk{
        .vht_capability_info = 0xaabbccdd,
        .supported_vht_mcs_and_nss_set = 0x0011223344556677,
    };

    auto ieee = VhtCapabilities::FromDdk(ddk);
    EXPECT_EQ(0xaabbccddU, ieee.vht_cap_info.val());
    EXPECT_EQ(0x0011223344556677U, ieee.vht_mcs_nss.val());

    auto ddk2 = ieee.ToDdk();
    EXPECT_EQ(ddk.vht_capability_info, ddk2.vht_capability_info);
    EXPECT_EQ(ddk.supported_vht_mcs_and_nss_set, ddk2.supported_vht_mcs_and_nss_set);
}

TEST(VhtOperation, DdkConversion) {
    wlan_vht_op ddk{
        .vht_cbw = 0x01,
        .center_freq_seg0 = 42,
        .center_freq_seg1 = 106,
        .basic_mcs = 0x1122,
    };

    auto ieee = VhtOperation::FromDdk(ddk);
    EXPECT_EQ(0x01U, ieee.vht_cbw);
    EXPECT_EQ(42U, ieee.center_freq_seg0);
    EXPECT_EQ(106U, ieee.center_freq_seg1);
    EXPECT_EQ(0x1122U, ieee.basic_mcs.val());

    auto ddk2 = ieee.ToDdk();
    EXPECT_EQ(ddk.vht_cbw, ddk2.vht_cbw);
    EXPECT_EQ(ddk.center_freq_seg0, ddk2.center_freq_seg0);
    EXPECT_EQ(ddk.center_freq_seg1, ddk2.center_freq_seg1);
    EXPECT_EQ(ddk.basic_mcs, ddk2.basic_mcs);
}

TEST(SupportedRate, Create) {
    SupportedRate rate = {};
    ASSERT_EQ(rate.rate(), 0);
    ASSERT_EQ(rate.is_basic(), 0);

    // Create a rate with basic bit set.
    rate = SupportedRate(0xF9);
    ASSERT_EQ(rate.rate(), 0x79);
    ASSERT_EQ(rate.is_basic(), 1);

    // Create a rate with basic bit set but explicitly override basic setting.
    rate = SupportedRate(0xF9, false);
    ASSERT_EQ(rate.rate(), 0x79);
    ASSERT_EQ(rate.is_basic(), 0);

    // Create a rate with explicitly setting basic bit.
    rate = SupportedRate::basic(0x79);
    ASSERT_EQ(rate.rate(), 0x79);
    ASSERT_EQ(rate.is_basic(), 1);
}

TEST(SupportedRate, ToUint8) {
    SupportedRate rate = {};
    ASSERT_EQ(static_cast<uint8_t>(rate), 0);

    rate = SupportedRate(0xF9);
    ASSERT_EQ(static_cast<uint8_t>(rate), 0xF9);

    rate = SupportedRate::basic(0x79);
    ASSERT_EQ(static_cast<uint8_t>(rate), 0xF9);
}

TEST(SupportedRate, Compare) {
    // Ignore basic bit when comparing rates.
    SupportedRate rate1(0x79);
    SupportedRate rate2(0xF9);
    ASSERT_TRUE(rate1 == rate2);
    ASSERT_FALSE(rate1 != rate2);
    ASSERT_FALSE(rate1 < rate2);
    ASSERT_FALSE(rate1 > rate2);

    // Test smaller.
    rate1 = SupportedRate(0x78);
    rate2 = SupportedRate(0xF9);
    ASSERT_FALSE(rate1 == rate2);
    ASSERT_TRUE(rate1 != rate2);
    ASSERT_TRUE(rate1 < rate2);
    ASSERT_FALSE(rate1 > rate2);

    // Test larger.
    rate1 = SupportedRate(0x7A);
    rate2 = SupportedRate(0xF9);
    ASSERT_FALSE(rate1 == rate2);
    ASSERT_TRUE(rate1 != rate2);
    ASSERT_FALSE(rate1 < rate2);
    ASSERT_TRUE(rate1 > rate2);
}

struct RateVector {
    std::vector<SupportedRate> ap;
    std::vector<SupportedRate> client;
    std::vector<SupportedRate> want;
};

TEST(Intersector, IntersectRates) {
    // Rates are in 0.5Mbps increment: 12 -> 6 Mbps, 11 -> 5.5 Mbps, etc.
    std::vector<RateVector> list = {
        {{}, {}, {}},
        {{SupportedRate(12)}, {SupportedRate(12)}, {SupportedRate(12)}},
        {{SupportedRate::basic(12)}, {SupportedRate(12)}, {SupportedRate::basic(12)}},
        {{SupportedRate(12)}, {SupportedRate::basic(12)}, {SupportedRate(12)}},
        {{SupportedRate::basic(12)}, {}, {}},
        {{}, {SupportedRate::basic(12)}, {}},
        {{SupportedRate(12)}, {}, {}},
        {{}, {SupportedRate(12)}, {}},
        {{SupportedRate::basic(12), SupportedRate(24)},
         {SupportedRate::basic(24), SupportedRate(12)},
         {SupportedRate::basic(12), SupportedRate(24)}},
        {{SupportedRate(24), SupportedRate::basic(12)},
         {SupportedRate(12), SupportedRate::basic(24)},
         {SupportedRate::basic(12), SupportedRate(24)}},
        {{SupportedRate(72), SupportedRate::basic(108), SupportedRate::basic(96)},
         {SupportedRate(96)},
         {SupportedRate::basic(96)}},
        {{SupportedRate(72), SupportedRate::basic(108), SupportedRate::basic(96)},
         {SupportedRate::basic(72)},
         {SupportedRate(72)}},
    };

    for (auto vec : list) {
        auto got = IntersectRatesAp(vec.ap, vec.client);
        EXPECT_EQ(vec.want, got);
    }
}

}  // namespace
}  // namespace wlan
