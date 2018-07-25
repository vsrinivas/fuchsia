// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/common/logging.h>
#include <wlan/common/stats.h>

#include <fuchsia/wlan/stats/cpp/fidl.h>
#include <gtest/gtest.h>

namespace wlan {

#define WLAN_STATS_GET(v) stats_.stats.v.count

namespace {

class StatsTest : public ::testing::Test {};

TEST_F(StatsTest, StatsReset) {
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

}  // namespace

}  // namespace wlan
