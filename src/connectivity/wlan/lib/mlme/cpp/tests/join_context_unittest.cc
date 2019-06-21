// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/wlan/mlme/cpp/fidl.h>
#include <gtest/gtest.h>
#include <wlan/mlme/client/join_context.h>

namespace wlan {
namespace {

namespace wlan_common = ::fuchsia::wlan::common;
namespace wlan_mlme = ::fuchsia::wlan::mlme;
using wlan_common::CBW;
using wlan_common::PHY;

struct TestVector {
  uint8_t bss_chan_primary;
  CBW bss_chan_cbw;
  PHY phy;
  CBW cbw;
  CBW want_cbw;  // Finale to be stored in JoinContext
};

TEST(JoinContext, Sanitize) {
  TestVector tvs[] = {
      // clang-format off
        // Nothing to sanitize
        {136, CBW::CBW40BELOW, PHY::HT, CBW::CBW40BELOW, CBW::CBW40BELOW},
        {136, CBW::CBW40BELOW, PHY::HT, CBW::CBW20, CBW::CBW20},
        {136, CBW::CBW40, PHY::HT, CBW::CBW20, CBW::CBW20},
        // CBW to be sanitized
        {136, CBW::CBW40, PHY::HT, CBW::CBW40, CBW::CBW20},
        {132, CBW::CBW40BELOW, PHY::HT, CBW::CBW40BELOW, CBW::CBW20}
      // clang-format on
  };

  for (auto tv : tvs) {
    wlan_mlme::BSSDescription bss;
    bss.chan.primary = tv.bss_chan_primary;
    bss.chan.cbw = tv.bss_chan_cbw;

    JoinContext jc(std::move(bss), tv.phy, tv.cbw);
    auto got = jc.channel().cbw;  // What is stored to Join Context
    auto want = static_cast<uint8_t>(tv.want_cbw);
    EXPECT_EQ(got, want);

    // Other than CBW shall remain the same
    EXPECT_EQ(jc.channel().primary, tv.bss_chan_primary);
  }
}  // namespace

}  // namespace
}  // namespace wlan
