// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/common/element.h>
#include <wlan/mlme/client/station.h>

#include <fuchsia/wlan/mlme/cpp/fidl.h>
#include <lib/fidl/cpp/vector.h>

#include <gtest/gtest.h>

#include <optional>
#include <vector>

namespace wlan {
namespace {

struct TestVector {
    std::vector<uint8_t> ap_basic_rate_set;
    std::vector<uint8_t> ap_op_rate_set;
    std::vector<SupportedRate> client_rates;
    std::optional<std::vector<SupportedRate>> want_rates;
};

namespace wlan_mlme = ::fuchsia::wlan::mlme;
using SR = SupportedRate;
constexpr auto SR_b = SupportedRate::basic;

// op_rate_set is a super set of basic_rate_set.
// Result is the intersection of ap_op_rate_set and client_rates.
// The basic-ness of client rates are disregarded and the basic-ness of AP is preserved.

void TestOnce(const TestVector& tv) {
    auto basic = ::fidl::VectorPtr(tv.ap_basic_rate_set);
    auto op = ::fidl::VectorPtr(tv.ap_op_rate_set);

    auto got_rates = BuildAssocReqSuppRates(basic, op, tv.client_rates);

    ASSERT_EQ(tv.want_rates.has_value(), got_rates.has_value());
    if (!got_rates.has_value()) { return; }

    EXPECT_EQ(tv.want_rates.value(), got_rates.value());
    for (size_t i = 0; i < got_rates->size(); ++i) {
        EXPECT_EQ(tv.want_rates.value()[i].val(), got_rates.value()[i].val());
    }
}

TEST(AssociationRatesTest, Success) {
    TestOnce({
        .ap_basic_rate_set = {1},
        .ap_op_rate_set = {1, 2},
        .client_rates = {SR{1}, SR{2}, SR{3}},
        .want_rates = {{SR_b(1), SR(2)}},
    });
}

TEST(AssociationRatesTest, SuccessWithDuplicateRates) {
    TestOnce({
        .ap_basic_rate_set = {1, 1},
        .ap_op_rate_set = {1},
        .client_rates = {SR{1}, SR{2}, SR{3}},
        .want_rates = {{SR_b(1)}},
    });
}

TEST(AssociationRatesTest, FailureNoApBasicRatesSupported) {
    TestOnce({
        .ap_basic_rate_set = {1},
        .ap_op_rate_set = {1},
        .client_rates = {SR{2}, SR{3}},
        .want_rates = {},
    });
}

TEST(AssociationRatesTest, FailureApBasicRatesPartiallySupported) {
    TestOnce({
        .ap_basic_rate_set = {1, 4},
        .ap_op_rate_set = {1, 4},
        .client_rates = {SR{1}, SR{2}, SR{3}},
        .want_rates = {},
    });
}
}  // namespace
}  // namespace wlan
