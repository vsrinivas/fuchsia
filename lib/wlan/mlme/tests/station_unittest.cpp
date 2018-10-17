// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/common/element.h>
#include <wlan/mlme/client/station.h>

#include <fuchsia/wlan/mlme/cpp/fidl.h>
#include <lib/fidl/cpp/vector.h>

#include <gtest/gtest.h>

#include <vector>

namespace wlan {
namespace {

struct TestVector {
    std::vector<uint8_t> ap_basic_rate_set;
    std::vector<uint8_t> ap_op_rate_set;
    std::vector<SupportedRate> client_supp_rates;
    std::vector<SupportedRate> client_ext_rates;
    std::vector<SupportedRate> want_supp_rates;
    std::vector<SupportedRate> want_ext_rates;
    zx_status_t want_status;
};

namespace wlan_mlme = ::fuchsia::wlan::mlme;
TEST(StationTest, BuildAssocReqSuppRates) {
    using SR = SupportedRate;
    constexpr auto SR_b = SupportedRate::basic;
    std::vector<TestVector> tvs{
        {
            {1},                    // ap_basic
            {1, 2},                 // ap_op
            {SR{1}, SR{2}, SR{3}},  // client_supp
            {},                     // client_ext
            {SR_b(1), SR_b(2)},     // want_supp
            {},                     // want_ext
            ZX_OK,                  // want_status
        },
        {
            {1, 2, 3, 4},                                                      // ap_basic
            {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12},                           // ap_op
            {SR(1), SR(2), SR(3), SR(4), SR(5), SR(6), SR(7), SR(8)},          // client_supp
            {SR(9), SR(10), SR(11), SR(12)},                                   // client_ext
            {SR_b(1), SR_b(2), SR_b(3), SR_b(4), SR(5), SR(6), SR(7), SR(8)},  // want_supp
            {SR(9), SR(10), SR(11), SR(12)},                                   // want_ext
            ZX_OK,                                                             // want_status
        },
        {
            {1},                    // ap_basic
            {1},                    // ap_op
            {SR{1}, SR{2}, SR{3}},  // client_supp
            {},                     // client_ext
            {SR_b(1)},              // want_supp
            {},                     // want_ext
            ZX_OK,                  // want_status
        },
        {
            {1},                   // ap_basic
            {1},                   // ap_op
            {SR{2}, SR{3}},        // client_supp
            {},                    // client_ext
            {},                    // want_supp
            {},                    // want_ext
            ZX_ERR_NOT_SUPPORTED,  // want_status
        },
        {
            {1, 4},                 // ap_basic
            {1, 4},                 // ap_op
            {SR{1}, SR{2}, SR{3}},  // client_supp
            {},                     // client_ext
            {SR_b(1)},              // want_supp
            {},                     // want_ext
            ZX_ERR_NOT_SUPPORTED,   // want_status
        },
    };

    for (auto tv : tvs) {
        wlan_mlme::BSSDescription bss{
            .basic_rate_set = ::fidl::VectorPtr(tv.ap_basic_rate_set),
            .op_rate_set = ::fidl::VectorPtr(tv.ap_op_rate_set),
        };
        AssocContext client{
            .supported_rates = tv.client_supp_rates,
            .ext_supported_rates = tv.client_ext_rates,
        };
        std::vector<SupportedRate> got_supp_rates;
        std::vector<SupportedRate> got_ext_rates;
        zx_status_t got_status =
            BuildAssocReqSuppRates(bss, client, &got_supp_rates, &got_ext_rates);
        EXPECT_EQ(tv.want_supp_rates, got_supp_rates);
        EXPECT_EQ(tv.want_ext_rates, got_ext_rates);
        EXPECT_EQ(tv.want_status, got_status);
    }
}

}  // namespace
}  // namespace wlan
