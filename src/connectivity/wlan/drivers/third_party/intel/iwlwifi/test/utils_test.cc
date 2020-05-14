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

#include <lib/mock-function/mock-function.h>
#include <stdio.h>

#include <zxtest/zxtest.h>

extern "C" {
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/mvm/mvm.h"
}

namespace {

class UtilsTest : public zxtest::Test {
 public:
  UtilsTest() {}
  ~UtilsTest() {}
};

TEST_F(UtilsTest, LegacyToDot11) {
  int idx;  // 802.11 index

  // The band is out of range
  EXPECT_EQ(iwl_mvm_legacy_rate_to_mac80211_idx(0, WLAN_INFO_BAND_COUNT, &idx),
            ZX_ERR_OUT_OF_RANGE);

  // Invalid pointer
  EXPECT_EQ(iwl_mvm_legacy_rate_to_mac80211_idx(0, WLAN_INFO_BAND_5GHZ, nullptr),
            ZX_ERR_INVALID_ARGS);

#if 0   // NEEDS_PORTING
  // Not supported 60GHz
  EXPECT_EQ(iwl_mvm_legacy_rate_to_mac80211_idx(0, NL80211_BAND_60GHZ, &idx), ZX_ERR_NOT_SUPPORTED);
#endif  // NEEDS_PORTING

  // 2.4 GHz: data rate: 1 Mbps ~ 54 Mbps
  EXPECT_EQ(iwl_mvm_legacy_rate_to_mac80211_idx(10 /* 1 Mbpsz */, WLAN_INFO_BAND_2GHZ, &idx),
            ZX_OK);
  EXPECT_EQ(idx, 0);
  EXPECT_EQ(iwl_mvm_legacy_rate_to_mac80211_idx(3 /* 54 Mbpsz */, WLAN_INFO_BAND_2GHZ, &idx),
            ZX_OK);
  EXPECT_EQ(idx, 11);

  // 5 GHz: data rate: 6 Mbps ~ 54 Mbps
  EXPECT_EQ(iwl_mvm_legacy_rate_to_mac80211_idx(13 /* 6 Mbps */, WLAN_INFO_BAND_5GHZ, &idx), ZX_OK);
  EXPECT_EQ(idx, 0);
  EXPECT_EQ(iwl_mvm_legacy_rate_to_mac80211_idx(3 /* 54 Mbps */, WLAN_INFO_BAND_5GHZ, &idx), ZX_OK);
  EXPECT_EQ(idx, 7);
  EXPECT_EQ(iwl_mvm_legacy_rate_to_mac80211_idx(10 /* 1 Mbps */, WLAN_INFO_BAND_5GHZ, &idx),
            ZX_ERR_NOT_FOUND);

  // Not in the table
  EXPECT_EQ(iwl_mvm_legacy_rate_to_mac80211_idx(0 /* random number */, WLAN_INFO_BAND_5GHZ, &idx),
            ZX_ERR_NOT_FOUND);
}

TEST_F(UtilsTest, Dot11ToHWRate) {
  EXPECT_EQ(iwl_mvm_mac80211_idx_to_hwrate(0 /* 1 Mbps */), 10);
  EXPECT_EQ(iwl_mvm_mac80211_idx_to_hwrate(11 /* 54 Mbps */), 3);
}

TEST_F(UtilsTest, Dot11ToDataRate) {
  uint32_t data_rate;

  // 2.4 GHz, idx: 0 ~ 11
  EXPECT_EQ(ZX_OK, mac80211_idx_to_data_rate(WLAN_INFO_BAND_2GHZ, 0 /* 1 Mbps */, &data_rate));
  EXPECT_EQ(data_rate, TO_HALF_MBPS(1));
  EXPECT_EQ(ZX_OK, mac80211_idx_to_data_rate(WLAN_INFO_BAND_2GHZ, 11 /* 54 Mbps */, &data_rate));
  EXPECT_EQ(data_rate, TO_HALF_MBPS(54));
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, mac80211_idx_to_data_rate(WLAN_INFO_BAND_2GHZ, 12, &data_rate));

  // 5 GHz, idx: 0 ~ 7
  EXPECT_EQ(ZX_OK, mac80211_idx_to_data_rate(WLAN_INFO_BAND_5GHZ, 0 /* 6 Mbps */, &data_rate));
  EXPECT_EQ(data_rate, TO_HALF_MBPS(6));
  EXPECT_EQ(ZX_OK, mac80211_idx_to_data_rate(WLAN_INFO_BAND_5GHZ, 7 /* 54 Mbps */, &data_rate));
  EXPECT_EQ(data_rate, TO_HALF_MBPS(54));
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, mac80211_idx_to_data_rate(WLAN_INFO_BAND_5GHZ, 8, &data_rate));

  // For future 60 GHz
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, mac80211_idx_to_data_rate(WLAN_INFO_BAND_COUNT, 0, &data_rate));
}

struct iter_data {
  size_t total_count;
  size_t associated_count;
};

static void iter_active_interface(void* data_, struct iwl_mvm_vif* mvmvif) {
  struct iter_data* data = (struct iter_data*)data_;

  data->total_count++;
  if (mvmvif->associated) {
    data->associated_count++;
  }
}

TEST_F(UtilsTest, IterateActiveInterfaces) {
  struct iwl_mvm_vif mvmvifs[] = {
      {
          .associated = true,
      },
      {
          // Will be NULLed later.
      },
      {
          .associated = false,
      },
      {
          .associated = true,
      },
  };
  uint8_t vif_count = ARRAY_SIZE(mvmvifs);
  ZX_ASSERT(vif_count <= MAX_NUM_MVMVIF);

  // Initialize the 'mvm' structure (and its 'mvmvif').
  struct iwl_mvm mvm = {
      .vif_count = vif_count,
  };
  for (size_t i = 0; i < vif_count; ++i) {
    mvm.mvmvif[i] = &mvmvifs[i];
  }
  mvm.mvmvif[1] = NULL;  // Set to NULL to test if the code can handle it.

  // The 'data' used for callback.
  struct iter_data data = {
      .total_count = 0,
      .associated_count = 0,
  };

  // Iterate it!
  mtx_lock(&mvm.mutex);
  ieee80211_iterate_active_interfaces_atomic(&mvm, iter_active_interface, &data);
  mtx_unlock(&mvm.mutex);

  EXPECT_EQ(3, data.total_count);       // 3 interfaces checked.
  EXPECT_EQ(2, data.associated_count);  // 2 interfaces associated.
}

}  // namespace
