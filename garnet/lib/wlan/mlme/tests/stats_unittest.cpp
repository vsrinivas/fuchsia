// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/common/logging.h>
#include <wlan/common/stats.h>

#include <fuchsia/wlan/stats/cpp/fidl.h>
#include <gtest/gtest.h>

namespace wlan {

#define WLAN_STATS_GET(v) stats_.stats.v.count
#define WLAN_RSSI_HIST_GET(s, i) stats_.stats.s.Get(i)

namespace {

class StatsTest : public ::testing::Test {};

TEST_F(StatsTest, DispatcherStatsReset) {
    common::WlanStats<common::DispatcherStats, ::fuchsia::wlan::stats::DispatcherStats> stats_;

    WLAN_STATS_INC(any_packet.in);
    WLAN_STATS_INC(any_packet.in);
    ASSERT_EQ(WLAN_STATS_GET(any_packet.in), 2u);

    WLAN_STATS_INC(any_packet.out);
    ASSERT_EQ(WLAN_STATS_GET(any_packet.out), 1u);

    WLAN_STATS_INC(any_packet.drop);
    ASSERT_EQ(WLAN_STATS_GET(any_packet.drop), 1u);

    stats_.Reset();

    ASSERT_EQ(WLAN_STATS_GET(any_packet.in), 0u);
    ASSERT_EQ(WLAN_STATS_GET(any_packet.out), 0u);
    ASSERT_EQ(WLAN_STATS_GET(any_packet.drop), 0u);
}

TEST_F(StatsTest, RssiStatsReset) {
    common::WlanStats<common::ClientMlmeStats, ::fuchsia::wlan::stats::ClientMlmeStats> stats_;

    ASSERT_EQ(WLAN_RSSI_HIST_GET(beacon_rssi, -5), 0u);
    ASSERT_EQ(WLAN_RSSI_HIST_GET(beacon_rssi, -77), 0u);

    WLAN_RSSI_HIST_INC(beacon_rssi, -5);
    WLAN_RSSI_HIST_INC(beacon_rssi, -77);
    WLAN_RSSI_HIST_INC(beacon_rssi, -77);

    ASSERT_EQ(WLAN_RSSI_HIST_GET(beacon_rssi, -5), 1u);
    ASSERT_EQ(WLAN_RSSI_HIST_GET(beacon_rssi, -77), 2u);

    stats_.Reset();

    ASSERT_EQ(WLAN_RSSI_HIST_GET(beacon_rssi, -5), 0u);
    ASSERT_EQ(WLAN_RSSI_HIST_GET(beacon_rssi, -77), 0u);
}

TEST_F(StatsTest, RssiStatsIncAndGet) {
    common::WlanStats<common::ClientMlmeStats, ::fuchsia::wlan::stats::ClientMlmeStats> stats_;

    ASSERT_EQ(WLAN_RSSI_HIST_INC(beacon_rssi, 0), 1u);
    ASSERT_EQ(WLAN_RSSI_HIST_GET(beacon_rssi, 0), 1u);

    ASSERT_EQ(WLAN_RSSI_HIST_INC(beacon_rssi, -::fuchsia::wlan::stats::RSSI_BINS + 1), 1u);
    ASSERT_EQ(WLAN_RSSI_HIST_GET(beacon_rssi, -::fuchsia::wlan::stats::RSSI_BINS + 1), 1u);

    ASSERT_EQ(WLAN_RSSI_HIST_INC(beacon_rssi, 1), 0u);
    ASSERT_EQ(WLAN_RSSI_HIST_GET(beacon_rssi, 1), 0u);

    ASSERT_EQ(WLAN_RSSI_HIST_INC(beacon_rssi, 50), 0u);
    ASSERT_EQ(WLAN_RSSI_HIST_GET(beacon_rssi, 50), 0u);
}

}  // namespace

}  // namespace wlan
