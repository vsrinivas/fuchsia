/*
 * Copyright (c) 2019 The Fuchsia Authors
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
// Unittest code for the functions in mvm/utils.c

#include <stdio.h>

#include "gtest/gtest.h"

extern "C" {
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/mvm/mvm.h"
}

namespace {

class UtilsTest : public testing::Test {
   public:
    UtilsTest() {}
    ~UtilsTest() {}
};

TEST_F(UtilsTest, LegacyToDot11) {
    int idx;  // 802.11 index

    // The band is out of range
    EXPECT_EQ(iwl_mvm_legacy_rate_to_mac80211_idx(0, NUM_NL80211_BANDS, &idx),
              ZX_ERR_OUT_OF_RANGE);

    // Invalid pointer
    EXPECT_EQ(iwl_mvm_legacy_rate_to_mac80211_idx(0, NL80211_BAND_5GHZ, nullptr),
              ZX_ERR_INVALID_ARGS);

    // Not supported 60GHz
    EXPECT_EQ(iwl_mvm_legacy_rate_to_mac80211_idx(0, NL80211_BAND_60GHZ, &idx),
              ZX_ERR_NOT_SUPPORTED);

    // 2.4 GHz: data rate: 1 MHz ~ 54 MHz
    EXPECT_EQ(iwl_mvm_legacy_rate_to_mac80211_idx(10 /* 1 Mhz */, NL80211_BAND_2GHZ, &idx), ZX_OK);
    EXPECT_EQ(idx, 0);
    EXPECT_EQ(iwl_mvm_legacy_rate_to_mac80211_idx(3 /* 54 Mhz */, NL80211_BAND_2GHZ, &idx), ZX_OK);
    EXPECT_EQ(idx, 11);

    // 5 GHz: data rate: 6 MHz ~ 54 MHz
    EXPECT_EQ(iwl_mvm_legacy_rate_to_mac80211_idx(13 /* 6 Mhz */, NL80211_BAND_5GHZ, &idx), ZX_OK);
    EXPECT_EQ(idx, 0);
    EXPECT_EQ(iwl_mvm_legacy_rate_to_mac80211_idx(3 /* 54 Mhz */, NL80211_BAND_5GHZ, &idx), ZX_OK);
    EXPECT_EQ(idx, 7);
    EXPECT_EQ(iwl_mvm_legacy_rate_to_mac80211_idx(10 /* 1 Mhz */, NL80211_BAND_5GHZ, &idx),
              ZX_ERR_NOT_FOUND);

    // Not in the table
    EXPECT_EQ(iwl_mvm_legacy_rate_to_mac80211_idx(0 /* random number */, NL80211_BAND_5GHZ, &idx),
              ZX_ERR_NOT_FOUND);
}

TEST_F(UtilsTest, Dot11ToHWRate) {
    EXPECT_EQ(iwl_mvm_mac80211_idx_to_hwrate(0 /* 1 Mhz */), 10);
    EXPECT_EQ(iwl_mvm_mac80211_idx_to_hwrate(11 /* 54 Mhz */), 3);
}

}  // namespace
