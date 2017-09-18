// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mac_frame.h"
#include "wlan.h"

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
    EXPECT_TRUE(TimElement::Create(buf_, sizeof(buf_), &actual_, 1, 2, bmp_ctrl, bmp));
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
    EXPECT_TRUE(TimElement::Create(buf_, sizeof(buf_), &actual_, 1, 2, bmp_ctrl, bmp));
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
    EXPECT_TRUE(TimElement::Create(buf_, sizeof(buf_), &actual_, 1, 2, bmp_ctrl, bmp));

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
    const char kCountry[3] = "US";
    EXPECT_TRUE(CountryElement::Create(buf_, sizeof(buf_), &actual_, kCountry));
    EXPECT_EQ(sizeof(CountryElement), actual_);

    auto element = FromBytes<CountryElement>(buf_, sizeof(buf_));
    ASSERT_NE(nullptr, element);
    EXPECT_EQ(0, std::memcmp(element->country, kCountry, sizeof(element->country)));
}

}  // namespace
}  // namespace wlan
