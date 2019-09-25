// Copyright 2019 The Fuchsia Authors. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include <string.h>

#include <gtest/gtest.h>
#include <ddk/protocol/wlanif.h>

TEST(WlanifImplUnittest, WlanifMlmeStats) {
  // Verify that union members of wlanif_mlme_stats do not overwrite each other.
  wlanif_mlme_stats_t stats = {};
  stats.tag = 0x1;
  memset(&stats.client_mlme_stats, 0x2, sizeof(stats.client_mlme_stats));
  stats.client_mlme_stats.tag = 0x1;
  EXPECT_EQ(stats.tag, 0x1);
  memset(&stats.ap_mlme_stats, 0x3, sizeof(stats.ap_mlme_stats));
  stats.client_mlme_stats.tag = 0x1;
  EXPECT_EQ(stats.tag, 0x1);
}
