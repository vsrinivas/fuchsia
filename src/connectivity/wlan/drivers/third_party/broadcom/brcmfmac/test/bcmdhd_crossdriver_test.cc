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

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <third_party/bcmdhd/crossdriver/bcmwifi_channels.h>
#include <third_party/bcmdhd/crossdriver/dhd.h>
#include <third_party/bcmdhd/crossdriver/wl_cfg80211.h>

namespace {

using ::testing::Each;

// This is a valid 2G chanspec captured from physical hardware: 20 MHz, channel 6.
const chanspec_t kChanspec2g20MhzCh6 = 0x1006;

// This is a valid 5G chanspec captured from physical hardware: 80 MHz, channel 42.
const chanspec_t kChanspec5g80MhzCh42 = 0xe02a;

TEST(BcmdhdCrossdriver, ChspecMalformedFalseForValid2GBand) {
  EXPECT_FALSE(chspec_malformed(kChanspec2g20MhzCh6));
};

TEST(BcmdhdCrossdriver, ChspecMalformedFalseForValid5GBand) {
  EXPECT_FALSE(chspec_malformed(kChanspec5g80MhzCh42));
};

TEST(BcmdhdCrossdriver, ChspecMalformedForInvalidBand) {
  // This chanspec does not match 2G or 5G bands.
  const chanspec_t invalid_chanspec = WL_CHANSPEC_BAND_3G;
  EXPECT_TRUE(chspec_malformed(invalid_chanspec));
};

TEST(BcmdhdCrossdriver, ChspecMalformedForInvalid2GBandwidth) {
  // This band + bandwidth combination is invalid.
  const chanspec_t invalid_chanspec = WL_CHANSPEC_BAND_2G | WL_CHANSPEC_BW_80;
  EXPECT_TRUE(chspec_malformed(invalid_chanspec));
};

TEST(BcmdhdCrossdriver, ChspecMalformedForInvalidChannel) {
  const chanspec_t invalid_chanspec = WL_CHANSPEC_BAND_5G | (MAXCHANNEL + 1);
  EXPECT_TRUE(chspec_malformed(invalid_chanspec));
};

TEST(BcmdhdCrossdriver, ControlChannelFromBadChspec) {
  // This band + bandwidth combination is invalid.
  chanspec_t invalid_chanspec = WL_CHANSPEC_BAND_2G | WL_CHANSPEC_BW_80;
  uint8_t ctl_chan;
  zx_status_t status = chspec_ctlchan(invalid_chanspec, &ctl_chan);
  EXPECT_NE(status, ZX_OK);
  // This band + channel combination is invalid.
  invalid_chanspec = WL_CHANSPEC_BAND_5G | (MAXCHANNEL + 1);
  status = chspec_ctlchan(invalid_chanspec, &ctl_chan);
  EXPECT_NE(status, ZX_OK);
};

TEST(BcmdhdCrossdriver, ControlChannelFromGoodChspec) {
  chanspec_t valid_chanspec = kChanspec5g80MhzCh42;
  uint8_t ctl_chan;
  zx_status_t status = chspec_ctlchan(valid_chanspec, &ctl_chan);
  EXPECT_EQ(status, ZX_OK);
  EXPECT_EQ(ctl_chan, 36);
}
// This is a smoke test. A wl_wstats_cnt_t with empty histograms is possible.
TEST(BcmdhdCrossdriver, GetHistogramsSucceedsWithEmptyWstatsCounters) {
  const wl_wstats_cnt_t wstats_cnt = {};
  // Version 2 indicates non-legacy chanspec.
  const uint32_t version = 2;
  const uint32_t rxchain = 1;
  histograms_report_t report;

  EXPECT_TRUE(get_histograms(wstats_cnt, kChanspec2g20MhzCh6, version, rxchain, &report));
  // With the given chanspec and rxchain, the histogram should have these fields set.
  EXPECT_EQ(report.antennaid.freq, ANTENNA_2G);
  EXPECT_EQ(report.antennaid.idx, 0);
  // All of the histograms should be empty.
  EXPECT_THAT(report.rxsnr, Each(0));
  EXPECT_THAT(report.rxnoiseflr, Each(0));
  EXPECT_THAT(report.rxrssi, Each(0));
  EXPECT_THAT(report.rx11b, Each(0));
  EXPECT_THAT(report.rx11g, Each(0));
  // rx11n is a multi-dimensional array: [SGI off/on][BW 20/40][MCS 0-15]
  for (const auto& per_gi_table : report.rx11n) {
    for (const auto& per_bw_table : per_gi_table) {
      EXPECT_THAT(per_bw_table, Each(0));
    }
  }
  // rx11ac is a multi-dimensional array: [NSS 1/2][SGI off/on][BW 20/40/80][MCS 0-9]
  for (const auto& per_nss_table : report.rx11ac) {
    for (const auto& per_gi_table : per_nss_table) {
      for (const auto& per_bw_table : per_gi_table) {
        EXPECT_THAT(per_bw_table, Each(0));
      }
    }
  }
}

TEST(BcmdhdCrossdriver, GetHistogramsSucceedsWithWstatsCountersHistograms) {
  // Version 2 indicates non-legacy chanspec.
  const uint32_t version = 2;
  const uint32_t rxchain = 1;

  // Create a wl_stats_t that will be input into get_histograms() below.
  wl_wstats_cnt_t wstats_cnt = {
      .version = WSTATS_CNT_T_VERSION,
  };

  // Manually populate a value in each wl_stats_t histogram. We will expect to see these values in
  // the histograms_report_t after the get_histograms() call.
  const uint16_t expected_snr_db = 60;
  const uint32_t expected_snr_num_frames = 50;
  wstats_cnt.rxsnr[expected_snr_db] = expected_snr_num_frames;

  const uint32_t expected_noiseflr_dbm_index = 170;
  const uint32_t expected_noiseflr_num_frames = 20;
  wstats_cnt.rxnoiseflr[expected_noiseflr_dbm_index] = expected_noiseflr_num_frames;

  const uint32_t expected_rssi_dbm_index = 190;
  const uint32_t expected_rssi_num_frames = 40;
  wstats_cnt.rxrssi[expected_rssi_dbm_index] = expected_rssi_num_frames;

  const uint32_t expected_11b_rate_index = 2;
  const uint32_t expected_11b_num_frames = 60;
  wstats_cnt.rx11b[expected_11b_rate_index] = expected_11b_num_frames;

  const uint32_t expected_11g_rate_index = 7;
  const uint32_t expected_11g_num_frames = 80;
  wstats_cnt.rx11g[expected_11g_rate_index] = expected_11g_num_frames;

  const uint8_t expected_sgi = 1;
  const uint8_t expected_bw = 1;
  const uint8_t expected_mcs = 9;
  const uint32_t expected_11n_num_frames = 20;
  wstats_cnt.rx11n[expected_sgi][expected_bw][expected_mcs] = expected_11n_num_frames;

  const uint8_t expected_nss = 1;
  const uint32_t expected_11ac_num_frames = 6500;
  wstats_cnt.rx11ac[expected_nss][expected_sgi][expected_bw][expected_mcs] =
      expected_11ac_num_frames;

  histograms_report_t report;
  EXPECT_TRUE(get_histograms(wstats_cnt, kChanspec5g80MhzCh42, version, rxchain, &report));
  // With the given chanspec and rxchain, the histogram should have these fields set.
  EXPECT_EQ(report.antennaid.freq, ANTENNA_5G);
  EXPECT_EQ(report.antennaid.idx, 0);
  // The fields that were manually set in the histograms should be present.
  EXPECT_EQ(report.rxsnr[expected_snr_db], expected_snr_num_frames);
  EXPECT_EQ(report.rxnoiseflr[expected_noiseflr_dbm_index], expected_noiseflr_num_frames);
  EXPECT_EQ(report.rxrssi[expected_rssi_dbm_index], expected_rssi_num_frames);
  EXPECT_EQ(report.rx11b[expected_11b_rate_index], expected_11b_num_frames);
  EXPECT_EQ(report.rx11n[expected_sgi][expected_bw][expected_mcs], expected_11n_num_frames);
  EXPECT_EQ(report.rx11ac[expected_nss][expected_sgi][expected_bw][expected_mcs],
            expected_11ac_num_frames);
}

TEST(BcmdhdCrossdriver, GetHistogramsFailsWithInvalidWstatsCountersVersion) {
  const wl_wstats_cnt_t wstats_cnt{.version = WSTATS_CNT_T_VERSION + 1};
  // Version 2 indicates non-legacy chanspec.
  const uint32_t version = 2;
  const uint32_t rxchain = 1;
  histograms_report_t report;
  EXPECT_FALSE(get_histograms(wstats_cnt, kChanspec2g20MhzCh6, version, rxchain, &report));
}

TEST(BcmdhdCrossdriver, GetHistogramsFailsWithInvalidChanspec) {
  const wl_wstats_cnt_t wstats_cnt{};
  const chanspec_t chanspec = INVCHANSPEC;
  // Version 2 indicates non-legacy chanspec.
  const uint32_t version = 2;
  const uint32_t rxchain = 1;
  histograms_report_t report;
  EXPECT_FALSE(get_histograms(wstats_cnt, chanspec, version, rxchain, &report));
}

}  // namespace
