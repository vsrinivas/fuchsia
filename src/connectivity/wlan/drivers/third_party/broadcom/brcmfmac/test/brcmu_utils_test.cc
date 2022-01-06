/*
 * Copyright (c) 2020 The Fuchsia Authors
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

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/brcmu_utils.h"

#include <gmock/gmock.h>

#include "gtest/gtest.h"

namespace {

using ::testing::ElementsAreArray;

TEST(BrcmuUtils, SetRxRateHistogram) {
  // RX rate histograms from firmware ("wstats_counters" iovar). These count the number
  // of frames received at each data rate for each PHY type.
  uint32_t rx11b[WSTATS_RATE_RANGE_11B] = {0, 1, 2, 3};
  uint32_t rx11g[WSTATS_RATE_RANGE_11G] = {4, 5, 6, 7, 8, 9, 10, 11};
  uint32_t rx11n[WSTATS_SGI_RANGE][WSTATS_BW_RANGE_11N][WSTATS_MCS_RANGE_11N] = {
      {
          {12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27},
          {28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43},
      },
      {
          {44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59},
          {60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75},
      },
  };
  uint32_t rx11ac[WSTATS_NSS_RANGE][WSTATS_SGI_RANGE][WSTATS_BW_RANGE_11AC][WSTATS_MCS_RANGE_11AC] =
      {{{{76, 77, 78, 79, 80, 81, 82, 83, 84, 85},
         {86, 87, 88, 89, 90, 91, 92, 93, 94, 95},
         {96, 97, 98, 99, 100, 101, 102, 103, 104, 105}},
        {{106, 107, 108, 109, 110, 111, 112, 113, 114, 115},
         {116, 117, 118, 119, 120, 121, 122, 123, 124, 125},
         {126, 127, 128, 129, 130, 131, 132, 133, 134, 135}}},
       {{{136, 137, 138, 139, 140, 141, 142, 143, 144, 145},
         {146, 147, 148, 149, 150, 151, 152, 153, 154, 155},
         {156, 157, 158, 159, 160, 161, 162, 163, 164, 165}},
        {{166, 167, 168, 169, 170, 171, 172, 173, 174, 175},
         {176, 177, 178, 179, 180, 181, 182, 183, 184, 185},
         {186, 187, 188, 189, 190, 191, 192, 193, 194, 195}}}};

  // The above data rates flattened into a single histogram.
  uint32_t expected_rx_rate[WLAN_FULLMAC_MAX_RX_RATE_INDEX_SAMPLES] = {
      // 802.11b
      0, 1, 2, 3,
      // 802.11g
      4, 5, 6, 7, 8, 9, 10, 11,
      // 802.11n 20Mhz, no SGI
      12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27,
      // 802.11n 40Mhz, no SGI
      28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43,
      // 802.11n 20Mhz, SGI
      44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59,
      // 802.11n 40Mhz, SGI
      60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75,

      // 802.11ac 20Mhz, no SGI, 1SS
      76, 77, 78, 79, 80, 81, 82, 83, 84, 85,
      // 802.11ac 20Mhz, no SGI, 2SS
      136, 137, 138, 139, 140, 141, 142, 143, 144, 145,
      // 802.11ac 40Mhz, no SGI, 1SS
      86, 87, 88, 89, 90, 91, 92, 93, 94, 95,
      // 802.11ac 40Mhz, no SGI, 2SS
      146, 147, 148, 149, 150, 151, 152, 153, 154, 155,
      // 802.11ac 80Mhz, no SGI, 1SS
      96, 97, 98, 99, 100, 101, 102, 103, 104, 105,
      // 802.11ac 80Mhz, no SGI, 2SS
      156, 157, 158, 159, 160, 161, 162, 163, 164, 165,

      // 802.11ac 20Mhz, SGI, 1SS
      106, 107, 108, 109, 110, 111, 112, 113, 114, 115,
      // 802.11ac 20Mhz, SGI, 2SS
      166, 167, 168, 169, 170, 171, 172, 173, 174, 175,
      // 802.11ac 40Mhz, SGI, 1SS
      116, 117, 118, 119, 120, 121, 122, 123, 124, 125,
      // 802.11ac 40Mhz, SGI, 2SS
      176, 177, 178, 179, 180, 181, 182, 183, 184, 185,
      // 802.11ac 80Mhz, SGI, 1SS
      126, 127, 128, 129, 130, 131, 132, 133, 134, 135,
      // 802.11ac 80Mhz, SGI, 2SS
      186, 187, 188, 189, 190, 191, 192, 193, 194, 195};

  uint32_t rx_rate[WLAN_FULLMAC_MAX_RX_RATE_INDEX_SAMPLES] = {0};

  brcmu_set_rx_rate_index_hist_rx11b(rx11b, rx_rate);
  brcmu_set_rx_rate_index_hist_rx11g(rx11g, rx_rate);
  brcmu_set_rx_rate_index_hist_rx11n(rx11n, rx_rate);
  brcmu_set_rx_rate_index_hist_rx11ac(rx11ac, rx_rate);

  EXPECT_THAT(rx_rate, ElementsAreArray(expected_rx_rate));
}

}  // namespace
