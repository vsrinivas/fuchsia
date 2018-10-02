// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include <wlan/common/channel.h>
#include <wlan/mlme/client/bss.h>

namespace wlan {
namespace {

namespace wlan_mlme = ::fuchsia::wlan::mlme;

TEST(BssTest, DeriveChannel) {
    wlan_mlme::BSSDescription desc;
    uint8_t bcn_chan = 0;
    bool has_dsss_param = false;
    uint8_t dsss_chan = 0;

    desc.ht_cap = nullptr;
    desc.ht_op = nullptr;
    desc.vht_cap = nullptr;
    desc.vht_op = nullptr;

    wlan_channel_t want = {
        .primary = 0,
        .cbw = CBW20,
        .secondary80 = 0,
    };

    // Zeros to Zeros
    wlan_channel_t got = DeriveChanFromBssDesc(desc, bcn_chan, has_dsss_param, dsss_chan);
    EXPECT_EQ(true, want == got);

    // bcn_chan is set to 0, but want is not updated.
    bcn_chan = 1;
    got = DeriveChanFromBssDesc(desc, bcn_chan, has_dsss_param, dsss_chan);
    EXPECT_EQ(false, want == got);

    // update want. Should match
    want.primary = 1;
    EXPECT_EQ(true, want == got);

    // dsss_chan is there, but has_dsss_chan is false. Hence ineffective.
    dsss_chan = 11;
    got = DeriveChanFromBssDesc(desc, bcn_chan, has_dsss_param, dsss_chan);
    EXPECT_EQ(true, want == got);

    // Now dsss_chan is effective, but want is not updated
    has_dsss_param = true;
    got = DeriveChanFromBssDesc(desc, bcn_chan, has_dsss_param, dsss_chan);
    EXPECT_EQ(false, want == got);

    // Update want, which matches dsss_chan.
    want.primary = 11;
    EXPECT_EQ(true, want == got);

    // Have ht_cap, but not ht_op.
    desc.ht_cap = wlan_mlme::HtCapabilities::New();
    desc.ht_cap->ht_cap_info.chan_width_set = to_enum_type(wlan_mlme::ChanWidthSet::TWENTY_FORTY);
    got = DeriveChanFromBssDesc(desc, bcn_chan, has_dsss_param, dsss_chan);
    EXPECT_EQ(true, want == got);

    // Now have ht_op, but note discrepancy the CBW infos in ht_cap and ht_op
    desc.ht_op = wlan_mlme::HtOperation::New();
    desc.ht_op->primary_chan = 6;
    desc.ht_op->ht_op_info.secondary_chan_offset =
        to_enum_type(wlan_mlme::SecChanOffset::SECONDARY_NONE);
    desc.ht_op->ht_op_info.sta_chan_width = to_enum_type(wlan_mlme::StaChanWidth::ANY);
    got = DeriveChanFromBssDesc(desc, bcn_chan, has_dsss_param, dsss_chan);
    EXPECT_EQ(false, want == got);

    // Now should use ht_op's primary channel, but still ht_cap's CBW is ignored.
    want.primary = 6;
    EXPECT_EQ(true, want == got);

    // Make ht_cap's and ht_op's CBW consistent, but want is not updated
    desc.ht_op->ht_op_info.secondary_chan_offset =
        to_enum_type(wlan_mlme::SecChanOffset::SECONDARY_ABOVE);
    got = DeriveChanFromBssDesc(desc, bcn_chan, has_dsss_param, dsss_chan);
    EXPECT_EQ(false, want == got);

    // Update the CBW in want
    want.cbw = CBW40;
    EXPECT_EQ(true, want == got);

    want.cbw = CBW40BELOW;
    EXPECT_EQ(false, want == got);

    want.cbw = CBW40ABOVE;
    EXPECT_EQ(true, want == got);

    // Make ht_cap's CBW inconsistent wrt ht_op.
    desc.ht_cap->ht_cap_info.chan_width_set = to_enum_type(wlan_mlme::ChanWidthSet::TWENTY_ONLY);
    got = DeriveChanFromBssDesc(desc, bcn_chan, has_dsss_param, dsss_chan);
    EXPECT_EQ(true, want == got);

    // HT CBW override test
    desc.ht_op->ht_op_info.sta_chan_width = to_enum_type(wlan_mlme::StaChanWidth::TWENTY);
    got = DeriveChanFromBssDesc(desc, bcn_chan, has_dsss_param, dsss_chan);
    EXPECT_EQ(false, want == got);
    want.cbw = CBW20;
    EXPECT_EQ(true, want == got);
    // Reset Override
    desc.ht_op->ht_op_info.sta_chan_width = to_enum_type(wlan_mlme::StaChanWidth::ANY);
    want.cbw = CBW40;
    got = DeriveChanFromBssDesc(desc, bcn_chan, has_dsss_param, dsss_chan);
    EXPECT_EQ(true, want == got);

    // Have vht_cap but without vht_op
    desc.vht_cap = wlan_mlme::VhtCapabilities::New();
    // Refer to IEEE Std 802.11-2016, Table 9-250, Table 9-252, Table 21-22, and Table 11-25
    // TODO(porce): Consider deeper coverage of this testing
    desc.vht_cap->vht_cap_info.supported_cbw_set = 2;
    got = DeriveChanFromBssDesc(desc, bcn_chan, has_dsss_param, dsss_chan);
    EXPECT_EQ(true, want == got);

    // Have vht_op
    desc.vht_op = wlan_mlme::VhtOperation::New();
    desc.vht_op->vht_cbw = to_enum_type(wlan_mlme::VhtCbw::CBW_20_40);
    desc.vht_op->center_freq_seg0 = 36;
    desc.vht_op->center_freq_seg1 = 0;
    desc.ht_op->primary_chan = 36;
    desc.ht_op->ht_op_info.secondary_chan_offset =
        to_enum_type(wlan_mlme::SecChanOffset::SECONDARY_NONE);
    want.primary = 36;
    want.cbw = CBW20;
    got = DeriveChanFromBssDesc(desc, bcn_chan, has_dsss_param, dsss_chan);
    EXPECT_EQ(true, want == got);

    desc.vht_op->center_freq_seg0 = 38;
    desc.ht_op->ht_op_info.secondary_chan_offset =
        to_enum_type(wlan_mlme::SecChanOffset::SECONDARY_ABOVE);
    want.cbw = CBW40;
    got = DeriveChanFromBssDesc(desc, bcn_chan, has_dsss_param, dsss_chan);
    EXPECT_EQ(true, want == got);

    // VHT CBW takes priority
    desc.vht_op->vht_cbw = to_enum_type(wlan_mlme::VhtCbw::CBW_80_160_80P80);
    desc.vht_op->center_freq_seg0 = 42;
    got = DeriveChanFromBssDesc(desc, bcn_chan, has_dsss_param, dsss_chan);
    EXPECT_EQ(false, want == got);
    want.cbw = CBW80;
    EXPECT_EQ(true, want == got);

    // Other CBWs
    desc.vht_op->center_freq_seg0 = 155;
    desc.vht_op->center_freq_seg1 = 106;
    want.cbw = CBW80P80;
    got = DeriveChanFromBssDesc(desc, bcn_chan, has_dsss_param, dsss_chan);
    EXPECT_EQ(true, want == got);

    desc.vht_op->center_freq_seg0 = 50;
    desc.vht_op->center_freq_seg1 = 42;
    want.cbw = CBW160;
    got = DeriveChanFromBssDesc(desc, bcn_chan, has_dsss_param, dsss_chan);
    EXPECT_EQ(true, want == got);

    // Invalid combo. Fallback to HT CBW.
    desc.vht_op->center_freq_seg0 = 50;
    desc.vht_op->center_freq_seg1 = 53;
    want.cbw = CBW40;
    got = DeriveChanFromBssDesc(desc, bcn_chan, has_dsss_param, dsss_chan);
    EXPECT_EQ(true, want == got);
}

struct TestVector {
    std::vector<uint8_t> supp_rates;
    std::vector<uint8_t> ext_supp_rates;
    std::vector<uint8_t> want_basic;
    std::vector<uint8_t> want_op;
};

TEST(BssTest, BasicRateSetAndOpRateSet) {
    std::vector<TestVector> tvs{
        // clang-format off
        {{111,},                       {},                           {},         {111,},    },
        {{},                           {111,},                       {},         {111,},    },
        {{SupportedRate::basic(111),}, {},                           {111,},     {111,},    },
        {{},                           {SupportedRate::basic(111),}, {111,},     {111,},    },
        {{97,},                        {SupportedRate::basic(111),}, {111,},     {97, 111,},},
        {{SupportedRate::basic(97),},  {111,},                       {97,},      {97, 111,},},
        {{SupportedRate::basic(97),},  {SupportedRate::basic(111),}, {97, 111,}, {97, 111,},},
        // clang-format on
    };
    for (auto tv : tvs) {
        fidl::VectorPtr<uint8_t> got_basic{};
        fidl::VectorPtr<uint8_t> got_op{};
        BuildMlmeRateSets(tv.supp_rates, tv.ext_supp_rates, &got_basic, &got_op);
        EXPECT_EQ(*got_basic, tv.want_basic);
        EXPECT_EQ(*got_op, tv.want_op);
    }
}
}  // namespace
}  // namespace wlan
