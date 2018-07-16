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

    EXPECT_TRUE(w.write<SsidElement>("test"));
    EXPECT_EQ(6u, w.size());

    std::vector<uint8_t> rates = {1, 2, 3, 4};
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
    const char kSsid[] = "test ssid";
    EXPECT_TRUE(SsidElement::Create(buf_, sizeof(buf_), &actual_, kSsid));
    EXPECT_EQ(sizeof(SsidElement) + strlen(kSsid), actual_);

    auto element = FromBytes<SsidElement>(buf_, sizeof(buf_));
    ASSERT_NE(nullptr, element);
    EXPECT_EQ(0, std::memcmp(kSsid, element->ssid, strlen(kSsid)));
}

TEST_F(Elements, SsidTooLong) {
    const char kSsid[] = "this ssid is too long to be a proper ssid";
    ASSERT_GT(strlen(kSsid), SsidElement::kMaxLen);
    EXPECT_FALSE(SsidElement::Create(buf_, sizeof(buf_), &actual_, kSsid));
}

TEST_F(Elements, SupportedRates) {
    std::vector<uint8_t> rates = {1, 2, 3};
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
    std::vector<uint8_t> rates = {1, 2, 3, 4, 5, 6, 7, 8, 9};
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
    VhtMcsNss vht_mcs_nss(0xaaaaaaaaaaaaaaaa);
    uint8_t vht_cbw = VhtOperation::VHT_CBW_80_160_80P80;
    uint8_t center_freq_seg0 = 155;
    uint8_t center_freq_seg1 = 42;

    EXPECT_TRUE(VhtOperation::Create(buf_, sizeof(buf_), &actual_, vht_cbw, center_freq_seg0,
                                     center_freq_seg1, vht_mcs_nss));
    EXPECT_EQ(sizeof(VhtOperation), actual_);

    auto element = FromBytes<VhtOperation>(buf_, sizeof(buf_));
    ASSERT_NE(nullptr, element);

    EXPECT_EQ(element->vht_cbw, vht_cbw);
    EXPECT_EQ(element->center_freq_seg0, center_freq_seg0);
    EXPECT_EQ(element->center_freq_seg1, center_freq_seg1);

    EXPECT_EQ(element->vht_mcs_nss.rx_max_mcs_ss1(), VhtMcsNss::VHT_MCS_0_TO_9);
    EXPECT_EQ(element->vht_mcs_nss.rx_max_mcs_ss2(), VhtMcsNss::VHT_MCS_0_TO_9);
    EXPECT_EQ(element->vht_mcs_nss.tx_max_mcs_ss4(), VhtMcsNss::VHT_MCS_0_TO_9);
    EXPECT_EQ(element->vht_mcs_nss.max_nsts(), 5);
    EXPECT_EQ(element->vht_mcs_nss.ext_nss_bw(), 1);
}

}  // namespace
}  // namespace wlan
