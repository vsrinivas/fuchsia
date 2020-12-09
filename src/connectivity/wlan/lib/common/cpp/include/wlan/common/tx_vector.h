// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_LIB_COMMON_CPP_INCLUDE_WLAN_COMMON_TX_VECTOR_H_
#define SRC_CONNECTIVITY_WLAN_LIB_COMMON_CPP_INCLUDE_WLAN_COMMON_TX_VECTOR_H_

#include <lib/zx/time.h>
#include <zircon/assert.h>
#include <zircon/types.h>

#include <cstdio>
#include <optional>

#include <ddk/hw/wlan/wlaninfo.h>
#include <wlan/common/element.h>
#include <wlan/common/logging.h>
#include <wlan/protocol/mac.h>

namespace wlan {

static constexpr uint8_t kHtNumMcs = 32;  // Only support MCS 0-31
static constexpr uint8_t kHtNumUniqueMcs = 8;
static constexpr uint8_t kErpNumTxVector = 8;

static constexpr tx_vec_idx_t kInvalidTxVectorIdx = WLAN_TX_VECTOR_IDX_INVALID;

static constexpr uint8_t kHtNumGi = 2;
static constexpr uint8_t kHtNumCbw = 2;
static constexpr uint8_t kHtNumTxVector = kHtNumGi * kHtNumCbw * kHtNumMcs;

static constexpr uint8_t kDsssCckNumTxVector = 4;

static constexpr tx_vec_idx_t kStartIdx = 1 + kInvalidTxVectorIdx;
static constexpr tx_vec_idx_t kHtStartIdx = kStartIdx;
static constexpr tx_vec_idx_t kErpStartIdx = kHtStartIdx + kHtNumTxVector;
static constexpr tx_vec_idx_t kDsssCckStartIdx = kErpStartIdx + kErpNumTxVector;
static constexpr tx_vec_idx_t kMaxValidIdx = kDsssCckStartIdx + kDsssCckNumTxVector - 1;

// Extend the definition of MCS index for ERP
// OFDM/ERP-OFDM, represented by WLAN_INFO_PHY_TYPE_ERP:
// 0: BPSK,   1/2 -> Data rate  6 Mbps
// 1: BPSK,   3/4 -> Data rate  9 Mbps
// 2: QPSK,   1/2 -> Data rate 12 Mbps
// 3: QPSK,   3/4 -> Data rate 18 Mbps
// 4: 16-QAM, 1/2 -> Data rate 24 Mbps
// 5: 16-QAM, 3/4 -> Data rate 36 Mbps
// 6: 64-QAM, 2/3 -> Data rate 48 Mbps
// 7: 64-QAM, 3/4 -> Data rate 54 Mbps
// DSSS, HR/DSSS, and ERP-DSSS/CCK, reprsented by WLAN_INFO_PHY_TYPE_DSSS and WLAN_INFO_PHY_TYPE_CCK
// 0:  2 -> 1   Mbps DSSS
// 1:  4 -> 2   Mbps DSSS
// 2: 11 -> 5.5 Mbps CCK
// 3: 22 -> 11  Mbps CCK

struct TxVector {
  wlan_info_phy_type_t phy;
  wlan_gi_t gi;
  wlan_channel_bandwidth_t cbw;
  // number of spatial streams, for VHT and beyond
  uint8_t nss;
  // For HT,  see IEEE 802.11-2016 Table 19-27
  // For VHT, see IEEE 802.11-2016 Table 21-30
  // For ERP, see FromSupportedRate() below (Fuchsia extension)
  uint8_t mcs_idx;

  static zx_status_t FromSupportedRate(const SupportedRate& rate, TxVector* tx_vec);
  static zx_status_t FromIdx(tx_vec_idx_t idx, TxVector* tx_vec);

  bool IsValid() const;
  zx_status_t ToIdx(tx_vec_idx_t* idx) const;
};

bool operator==(const TxVector& lhs, const TxVector& rhs);
bool operator!=(const TxVector& lhs, const TxVector& rhs);
std::optional<SupportedRate> TxVectorIdxToErpRate(tx_vec_idx_t idx);
static constexpr bool IsHt(tx_vec_idx_t idx) {
  return kHtStartIdx <= idx && idx < kHtStartIdx + kHtNumTxVector;
}
}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_LIB_COMMON_CPP_INCLUDE_WLAN_COMMON_TX_VECTOR_H_
