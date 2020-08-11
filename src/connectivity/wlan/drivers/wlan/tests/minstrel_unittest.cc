// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "../minstrel.h"

#include <lib/timekeeper/test_clock.h>
#include <test_timer.h>

#include <iterator>

#include <ddk/protocol/wlan/info.h>
#include <fbl/algorithm.h>
#include <gtest/gtest.h>
#include <wlan/mlme/timer.h>
#include <wlan/mlme/timer_manager.h>

#include "../probe_sequence.h"

namespace wlan {
namespace {

namespace wlan_minstrel = ::fuchsia::wlan::minstrel;

ProbeSequence::ProbeTable SequentialTable() {
  ProbeSequence::ProbeTable sequence_table;
  for (uint8_t i = 0; i < ProbeSequence::kNumProbeSequece; ++i) {
    for (tx_vec_idx_t j = kStartIdx; j <= kMaxValidIdx; ++j) {
      sequence_table[i][j - kStartIdx] = j;
    }
  }
  return sequence_table;
}

static const common::MacAddr kTestMacAddr({50, 53, 51, 56, 55, 52});
static const uint8_t kBasicRateBit = 0b10000000;

struct MinstrelTest : public ::testing::Test {
  MinstrelTest()
      : minstrel_(MinstrelRateSelector(std::make_unique<TestTimer>(0, &clock),
                                       ProbeSequence(SequentialTable()), zx::msec(100))) {
    kTestMacAddr.CopyTo(assoc_ctx_ht_.bssid);
  }

  void AdvanceTimeBy(zx::duration duration) { clock.Set(clock.Now() + duration); }

  timekeeper::TestClock clock;
  MinstrelRateSelector minstrel_;
  wlan_assoc_ctx_t assoc_ctx_ht_{
      .rates_cnt = 12,
      .rates = {2, 4, 11, 22, 12 | kBasicRateBit, 18, 24, 36, 48, 72, 96, 108 | kBasicRateBit},
      .has_ht_cap = true,
      .ht_cap =
          {
              // left->right: SGI 40 MHz, SGI 20 MHz, 40 MHz
              .ht_capability_info = 0b01100010,
              .supported_mcs_set =
                  {
                      .bytes =
                          {
                              0xff,  // MCS 0-7
                              0xff,  // MCS 8-15
                          },
                  },
          },
  };
  // Note: 16 is the max throughput and 136 is the highest basic rate. They will
  // never be probed.
  const tx_vec_idx_t want_probe_idx_[22] =
      // clang-format off
        {1,   2,   3,   4,   5,   6,   7,   8,
         9,   10,  11,  12,  13,  14,  15,  /* 16 ,*/
         129, 130, 131, 132, 133, 134, 135, /* 136 */};
  // clang-format on
};

TEST_F(MinstrelTest, AddPeer) {
  minstrel_.AddPeer(assoc_ctx_ht_);
  EXPECT_TRUE(minstrel_.IsActive());

  wlan_minstrel::Peers peers;
  zx_status_t status = minstrel_.GetListToFidl(&peers);
  EXPECT_EQ(ZX_OK, status);
  EXPECT_EQ(1ULL, peers.peers.size());

  wlan_minstrel::Peer peer;
  status = minstrel_.GetStatsToFidl(kTestMacAddr, &peer);
  EXPECT_EQ(ZX_OK, status);
  EXPECT_EQ(kTestMacAddr, common::MacAddr(peer.mac_addr.data()));
  // TODO(eyw): size would be 40 if 40 MHz is supported, 72 if 40 MHz and SGI
  // are both supported.
  EXPECT_EQ(24ULL, peer.entries.size());
  EXPECT_EQ(16, peer.max_tp);
  EXPECT_EQ(peer.entries[0].tx_vector_idx, peer.max_probability);
  EXPECT_EQ(kErpStartIdx + kErpNumTxVector - 1, peer.basic_highest);
  EXPECT_EQ(kErpStartIdx + kErpNumTxVector - 1, peer.basic_max_probability);
}

TEST_F(MinstrelTest, RemovePeer) {
  // Add a peer to be removed later.
  minstrel_.AddPeer(assoc_ctx_ht_);
  EXPECT_TRUE(minstrel_.IsActive());

  wlan_minstrel::Peers peers;
  zx_status_t status = minstrel_.GetListToFidl(&peers);
  EXPECT_EQ(ZX_OK, status);
  EXPECT_EQ(1ULL, peers.peers.size());

  // Remove the peer using its mac address.
  minstrel_.RemovePeer(kTestMacAddr);
  EXPECT_FALSE(minstrel_.IsActive());

  status = minstrel_.GetListToFidl(&peers);
  EXPECT_EQ(ZX_OK, status);
  EXPECT_TRUE(peers.peers.empty());

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
      .tx_status_entry =
          {
              // HT, CBW20, GI 800 ns,
              {16, 1},  // MCS 7, fail
              {15, 1},  // MCS 6, fail
              {14, 1},  // MCS 5, fail
              {13, 1},  // MCS 4, succeed because |.success| is true
          },
      .success = true,
  };

  clock.Set(zx::time(0));
  minstrel_.AddPeer(assoc_ctx_ht_);
  kTestMacAddr.CopyTo(tx_status.peer_addr);

  minstrel_.HandleTxStatusReport(tx_status);
  wlan_minstrel::Peer peer;
  EXPECT_EQ(ZX_OK, minstrel_.GetStatsToFidl(kTestMacAddr, &peer));
  // tx_status collected but NOT been processed yet
  // it will be processed every 100 ms, when HandleTimeout() is called.
  EXPECT_EQ(16, peer.max_tp);
  EXPECT_EQ(peer.entries[0].tx_vector_idx, peer.max_probability);

  AdvanceTimeBy(zx::msec(100));
  EXPECT_TRUE(minstrel_.HandleTimeout());  // tx_status are processed at HandleTimeout()
  EXPECT_EQ(ZX_OK, minstrel_.GetStatsToFidl(kTestMacAddr, &peer));
  EXPECT_EQ(13, peer.max_tp);           // Everything above 45 has 0 success, thus 0 throughput
  EXPECT_EQ(13, peer.max_probability);  // 45 has 100% success rate

  // 45 fails, but 41 (MCS 0) succeeds because |.success| is still true
  wlan_tx_status_entry_t entries[WLAN_TX_STATUS_MAX_ENTRY]{{13, 1}, {9, 1}};
  memcpy(tx_status.tx_status_entry, &entries, sizeof(tx_status.tx_status_entry));
  // after every cycle, success rate of 45 decrease to 75% of it previous value,
  // success rate of 41 stays at 100% because of continuous positive outcome
  // After enough cycles, 45's (success_rate * theoretical_throughput) becomes
  // lower than 41.
  for (int i = 0; i < 10; ++i) {
    minstrel_.HandleTxStatusReport(tx_status);
    AdvanceTimeBy(zx::msec(100));
    EXPECT_TRUE(minstrel_.HandleTimeout());
    EXPECT_EQ(ZX_OK, minstrel_.GetStatsToFidl(kTestMacAddr, &peer));
    EXPECT_EQ(9, peer.max_probability);
  }
  EXPECT_EQ(ZX_OK, minstrel_.GetStatsToFidl(kTestMacAddr, &peer));
  EXPECT_EQ(9, peer.max_tp);
}

TEST_F(MinstrelTest, HtIsMyFavorite) {
  wlan_tx_status_t failed_ht_tx_status{
      .tx_status_entry =
          {
              // MCS 8-15 all fail
              {kHtStartIdx + 15, 1},
              {kHtStartIdx + 14, 1},
              {kHtStartIdx + 13, 1},
              {kHtStartIdx + 12, 1},
              {kHtStartIdx + 11, 1},
              {kHtStartIdx + 10, 1},
              {kHtStartIdx + 9, 1},
              {kHtStartIdx + 8, 1},
          },
      .success = false,
  };

  wlan_tx_status_t ht_tx_status{
      .tx_status_entry =
          {
              // MCS 1-7 all fail, only MCS 0 succeeds.
              {kHtStartIdx + 7, 1},
              {kHtStartIdx + 6, 1},
              {kHtStartIdx + 5, 1},
              {kHtStartIdx + 4, 1},
              {kHtStartIdx + 3, 1},
              {kHtStartIdx + 2, 1},
              {kHtStartIdx + 1, 1},
              // Lowest HT MCS with success probability 11% == (1/9) note: 0.1f
              // < 1 - 0.9f
              {kHtStartIdx + 0, 9},
          },
      .success = true,
  };

  wlan_tx_status_t erp_tx_status{
      .tx_status_entry =
          {
              // Highest ERP rate with success probability 100% == (1/1)
              {kErpStartIdx + kErpNumTxVector - 1, 1},
          },
      .success = true,
  };

  clock.Set(zx::time(0));
  minstrel_.AddPeer(assoc_ctx_ht_);
  kTestMacAddr.CopyTo(failed_ht_tx_status.peer_addr);
  kTestMacAddr.CopyTo(ht_tx_status.peer_addr);
  kTestMacAddr.CopyTo(erp_tx_status.peer_addr);

  minstrel_.HandleTxStatusReport(failed_ht_tx_status);
  minstrel_.HandleTxStatusReport(ht_tx_status);
  minstrel_.HandleTxStatusReport(erp_tx_status);
  AdvanceTimeBy(zx::msec(100));
  ASSERT_TRUE(minstrel_.HandleTimeout());  // tx_status are processed at HandleTimeout()
  wlan_minstrel::Peer peer;
  EXPECT_EQ(ZX_OK, minstrel_.GetStatsToFidl(kTestMacAddr, &peer));
  // HT is selected for max_tp even though it has lower throughput
  EXPECT_EQ(kHtStartIdx, peer.max_tp);
  // HT is selected for max_probability even though it has lower probability
  EXPECT_EQ(kHtStartIdx, peer.max_probability);
}

std::unordered_set<tx_vec_idx_t> GetAllIndices(const wlan_minstrel::Peer& peer) {
  std::unordered_set<tx_vec_idx_t> indices;
  for (const auto& entry : peer.entries) {
    indices.emplace(entry.tx_vector_idx);
  }
  return indices;
}

TEST_F(MinstrelTest, AddMissingTxVector) {
  clock.Set(zx::time(0));

  assoc_ctx_ht_.rates_cnt = 10;
  const uint8_t fewer_rates[10] = {2, 4, 11, 22, 12, 18, 24, 36, 48, 72};  // missing 96 and 108
  std::copy(std::cbegin(fewer_rates), std::cend(fewer_rates), assoc_ctx_ht_.rates);
  minstrel_.AddPeer(assoc_ctx_ht_);

  wlan_tx_status_t tx_status{
      .tx_status_entry =
          {
              // ERP, CBW20, GI 800 ns,
              {kErpStartIdx + kErpNumTxVector - 1, 1},  // MCS 7, 108, non-present, fail
              {kErpStartIdx + kErpNumTxVector - 3, 1},  // MCS 5, 72,  present,     succeed
          },
      .success = true,
  };
  kTestMacAddr.CopyTo(tx_status.peer_addr);

  wlan_minstrel::Peer peer;
  EXPECT_EQ(ZX_OK, minstrel_.GetStatsToFidl(kTestMacAddr, &peer));
  auto indices = GetAllIndices(peer);
  EXPECT_FALSE(indices.count(kErpStartIdx + kErpNumTxVector - 1));

  minstrel_.HandleTxStatusReport(tx_status);
  EXPECT_EQ(ZX_OK, minstrel_.GetStatsToFidl(kTestMacAddr, &peer));
  indices = GetAllIndices(peer);
  EXPECT_TRUE(indices.count(kErpStartIdx + kErpNumTxVector - 1));
}

TEST_F(MinstrelTest, DataFramesEligibleForProbing) {
  clock.Set(zx::time(0));
  minstrel_.AddPeer(assoc_ctx_ht_);
  wlan_minstrel::Peer peer;
  EXPECT_EQ(ZX_OK, minstrel_.GetStatsToFidl(kTestMacAddr, &peer));

  FrameControl fc;
  fc.set_type(FrameType::kData);
  for (tx_vec_idx_t i = 0; i < kProbeInterval * std::size(want_probe_idx_); ++i) {
    const tx_vec_idx_t idx = minstrel_.GetTxVectorIdx(fc, kTestMacAddr, 0);
    if (i % kProbeInterval == kProbeInterval - 1) {
      tx_vec_idx_t want = want_probe_idx_[(i + 1) / kProbeInterval - 1];
      if (want != idx) {
        printf("probe mismatch #%d\n", i);
      }
      EXPECT_EQ(want, idx);
    } else {
      if (peer.max_tp != idx) {
        printf("non-probe mismatch #%d\n", i);
      }
      EXPECT_EQ(peer.max_tp, idx);
    }
  }
}

}  // namespace
}  // namespace wlan
