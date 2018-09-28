// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "../minstrel.h"

#include <lib/timekeeper/test_clock.h>
#include <test_timer.h>  // //garnet/lib/wlan/mlme/tests
#include <wlan/mlme/timer.h>
#include <wlan/mlme/timer_manager.h>

namespace wlan {
namespace {

namespace wlan_minstrel = ::fuchsia::wlan::minstrel;

static const common::MacAddr kTestMacAddr({50, 53, 51, 56, 55, 52});

struct MinstrelTest : public ::testing::Test {
    MinstrelTest()
        : minstrel_(MinstrelRateSelector(TimerManager(fbl::make_unique<TestTimer>(0, &clock)))) {
        kTestMacAddr.CopyTo(assoc_ctx_ht_.bssid);
    }

    void AdvanceTimeBy(zx::duration duration) { clock.Set(clock.Now() + duration); }

    timekeeper::TestClock clock;
    MinstrelRateSelector minstrel_;
    wlan_assoc_ctx_t assoc_ctx_ht_{
        .supported_rates_cnt = 8,
        .supported_rates = {2, 4, 11, 22, 12, 18, 24, 36},
        .ext_supported_rates_cnt = 4,
        .ext_supported_rates = {48, 72, 96, 108},
        .has_ht_cap = true,
        .ht_cap =
            {
                // left->right: SGI 40 MHz, SGI 20 MHz, 40 MHz
                .ht_capability_info = 0b01100010,
                .supported_mcs_set =
                    {
                        0xff,  // MCS 0-7
                        0xff,  // MCS 8-15
                    },
            },
    };
};

TEST_F(MinstrelTest, AddPeer) {
    minstrel_.AddPeer(assoc_ctx_ht_);
    EXPECT_TRUE(minstrel_.IsActive());

    wlan_minstrel::Peers peers;
    zx_status_t status = minstrel_.GetListToFidl(&peers);
    EXPECT_EQ(ZX_OK, status);
    EXPECT_EQ(1ULL, peers.peers->size());

    wlan_minstrel::Peer peer;
    status = minstrel_.GetStatsToFidl(kTestMacAddr, &peer);
    EXPECT_EQ(ZX_OK, status);
    EXPECT_EQ(kTestMacAddr, common::MacAddr(peer.mac_addr.data()));
    // TODO(eyw): size would be 72 if SGI is supported.
    EXPECT_EQ(40ULL, peer.entries->size());
    EXPECT_EQ(48, peer.max_tp);
    EXPECT_EQ((*peer.entries)[0].tx_vector_idx, peer.max_probability);
}

TEST_F(MinstrelTest, RemovePeer) {
    // Add a peer to be removed later.
    minstrel_.AddPeer(assoc_ctx_ht_);
    EXPECT_TRUE(minstrel_.IsActive());

    wlan_minstrel::Peers peers;
    zx_status_t status = minstrel_.GetListToFidl(&peers);
    EXPECT_EQ(ZX_OK, status);
    EXPECT_EQ(1ULL, peers.peers->size());

    // Remove the peer using its mac address.
    minstrel_.RemovePeer(kTestMacAddr);
    EXPECT_FALSE(minstrel_.IsActive());

    status = minstrel_.GetListToFidl(&peers);
    EXPECT_EQ(ZX_OK, status);
    EXPECT_TRUE(peers.peers->empty());

    wlan_minstrel::Peer peer;
    status = minstrel_.GetStatsToFidl(kTestMacAddr, &peer);
    EXPECT_EQ(ZX_ERR_NOT_FOUND, status);
}

TEST_F(MinstrelTest, HandleTimeout) {
    clock.Set(zx::time(0));
    minstrel_.AddPeer(assoc_ctx_ht_);

    AdvanceTimeBy(zx::msec(99));
    EXPECT_FALSE(minstrel_.HandleTimeout());
    AdvanceTimeBy(zx::msec(1));
    EXPECT_TRUE(minstrel_.HandleTimeout());
}

TEST_F(MinstrelTest, UpdateStats) {
    // .tx_status_entry contains up to 8 entries
    // All entries except the last one indicates failed attempts.
    // The last entry can be successful or unsuccessful based on .success
    wlan_tx_status_t tx_status{
        .success = true,
        .tx_status_entry =
            {
                // HT, CBW20, GI 800 ns,
                {48, 1},  // MCS 7, fail
                {47, 1},  // MCS 6, fail
                {46, 1},  // MCS 5, fail
                {45, 1},  // MCS 4, succeed because |.success| is true
            },
    };

    clock.Set(zx::time(0));
    minstrel_.AddPeer(assoc_ctx_ht_);
    kTestMacAddr.CopyTo(tx_status.peer_addr);

    minstrel_.HandleTxStatusReport(tx_status);
    wlan_minstrel::Peer peer;
    EXPECT_EQ(ZX_OK, minstrel_.GetStatsToFidl(kTestMacAddr, &peer));
    // tx_status collected but NOT been processed yet
    // it will be processed every 100 ms, when HandleTimeout() is called.
    EXPECT_EQ(48, peer.max_tp);
    EXPECT_EQ((*peer.entries)[0].tx_vector_idx, peer.max_probability);

    AdvanceTimeBy(zx::msec(100));
    EXPECT_TRUE(minstrel_.HandleTimeout());  // tx_status are processed at HandleTimeout()
    EXPECT_EQ(ZX_OK, minstrel_.GetStatsToFidl(kTestMacAddr, &peer));
    EXPECT_EQ(45, peer.max_tp);           // Everything above 45 has 0 success, thus 0 throughput
    EXPECT_EQ(45, peer.max_probability);  // 45 has 100% success rate

    // 45 fails, but 41 (MCS 0) succeeds because |.success| is still true
    wlan_tx_status_entry_t entries[WLAN_TX_STATUS_MAX_ENTRY]{{45, 1}, {41, 1}};
    memcpy(tx_status.tx_status_entry, &entries, sizeof(tx_status.tx_status_entry));
    // after every cycle, success rate of 45 decrease to 75% of it previous value,
    // success rate of 41 stays at 100% because of continuous positive outcome
    // After enough cycles, 45's (success_rate * theoretical_throughput) becomes lower than 41.
    for (int i = 0; i < 10; ++i) {
        minstrel_.HandleTxStatusReport(tx_status);
        AdvanceTimeBy(zx::msec(100));
        EXPECT_TRUE(minstrel_.HandleTimeout());
        EXPECT_EQ(ZX_OK, minstrel_.GetStatsToFidl(kTestMacAddr, &peer));
        EXPECT_EQ(41, peer.max_probability);
    }
    EXPECT_EQ(ZX_OK, minstrel_.GetStatsToFidl(kTestMacAddr, &peer));
    EXPECT_EQ(41, peer.max_tp);
}

}  // namespace
}  // namespace wlan
