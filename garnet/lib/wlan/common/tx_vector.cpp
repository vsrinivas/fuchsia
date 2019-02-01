// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/common/tx_vector.h>

namespace wlan {

//
// For HT:
// Changing CBW from 20 MHz to 40 MHz advances index by 32
// Changing GI  from 800 ns to 400 ns advances index by 64
//
//  Group   tx_vec_idx_t range    PHY   GI   CBW NSS MCS_IDX
//  0         1 -  32             HT    800  20  -   0-31
//  1        33 -  64             HT    800  40  -   0-31
//  2        65 -  96             HT    400  20  -   0-31
//  3        97 - 128             HT    400  40  -   0-31
//  4       129 - 136             ERP   -    -   -   0-7
//  5       137 - 138             DSSS  -    -   -   0-1
//  6       139 - 140             CCK   -    -   -   2-3
//
//  Note: MCS_IDX is explained in the definition of TxVector.
//
// TODO(NET-1451) VHT will be inserted between HT and ERP.

zx_status_t TxVector::FromSupportedRate(const SupportedRate& erp_rate, TxVector* tx_vec) {
    if (tx_vec == nullptr) {
        errorf("nullptr passed to TxVector::FromSupportedRate()\n");
        ZX_DEBUG_ASSERT(false);
        return ZX_ERR_INVALID_ARGS;
    }
    *tx_vec = TxVector{
        .gi = WLAN_GI_800NS,
        .cbw = CBW20,
        .nss = 1,
    };

    PHY phy;
    uint8_t mcs_idx;
    uint8_t rate_val = erp_rate.rate();

    switch (rate_val) {
    case 2:
        phy = WLAN_PHY_DSSS;
        mcs_idx = 0;
        break;
    case 4:
        phy = WLAN_PHY_DSSS;
        mcs_idx = 1;
        break;
    case 11:
        phy = WLAN_PHY_CCK;
        mcs_idx = 2;
        break;
    case 22:
        phy = WLAN_PHY_CCK;
        mcs_idx = 3;
        break;
    case 12:
        phy = WLAN_PHY_ERP;
        mcs_idx = 0;
        break;
    case 18:
        phy = WLAN_PHY_ERP;
        mcs_idx = 1;
        break;
    case 24:
        phy = WLAN_PHY_ERP;
        mcs_idx = 2;
        break;
    case 36:
        phy = WLAN_PHY_ERP;
        mcs_idx = 3;
        break;
    case 48:
        phy = WLAN_PHY_ERP;
        mcs_idx = 4;
        break;
    case 72:
        phy = WLAN_PHY_ERP;
        mcs_idx = 5;
        break;
    case 96:
        phy = WLAN_PHY_ERP;
        mcs_idx = 6;
        break;
    case 108:
        phy = WLAN_PHY_ERP;
        mcs_idx = 7;
        break;
    default:
        errorf("Invalid rate %u * 0.5 Mbps for 802.11a/b/g.\n", rate_val);
        ZX_DEBUG_ASSERT(false);
        return ZX_ERR_INVALID_ARGS;
    }
    tx_vec->phy = phy;
    tx_vec->mcs_idx = mcs_idx;
    return ZX_OK;
}

// Inverse of FromSupportedRate above, supports ERP rates but not CCK rates.
std::optional<SupportedRate> TxVectorIdxToErpRate(tx_vec_idx_t idx) {
  if (idx < kErpStartIdx || idx >= kErpStartIdx + kErpNumTxVector) { return {}; }
  constexpr uint16_t erp_rate_list[] = {12, 18, 34, 36, 48, 72, 96, 108};
  return SupportedRate(erp_rate_list[idx - kErpStartIdx]);
}

bool IsTxVecIdxValid(tx_vec_idx_t idx) {
    return kInvalidTxVectorIdx < idx && idx <= kMaxValidIdx;
}

PHY TxVecIdxToPhy(tx_vec_idx_t idx) {
    if (idx < kHtStartIdx + kHtNumTxVector) {
        return WLAN_PHY_HT;
    } else if (idx < kErpStartIdx + kErpNumTxVector) {
        return WLAN_PHY_ERP;
    } else if (idx < kDsssCckStartIdx + kDsssCckNumTxVector) {
        return idx - kDsssCckStartIdx < 2 ? WLAN_PHY_DSSS : WLAN_PHY_CCK;
    }
    // caller will always call IsTxVecIdxValie() so that this is never reached.
    ZX_DEBUG_ASSERT(false);
    return WLAN_PHY_HT;
}

zx_status_t TxVector::FromIdx(tx_vec_idx_t idx, TxVector* tx_vec) {
    if (!IsTxVecIdxValid(idx)) {
        errorf("Invalid idx for TxVector::FromIdx(): %u\n", idx);
        ZX_DEBUG_ASSERT(false);
        return ZX_ERR_INVALID_ARGS;
    }
    if (tx_vec == nullptr) {
        errorf("nullptr for TxVector::FromIdx()\n");
        ZX_DEBUG_ASSERT(false);
        return ZX_ERR_INVALID_ARGS;
    }
    PHY phy = TxVecIdxToPhy(idx);
    switch (phy) {
    case WLAN_PHY_HT: {
        uint8_t group_idx = (idx - kHtStartIdx) / kHtNumMcs;
        GI gi = ((group_idx / kHtNumCbw) % kHtNumGi == 1 ? WLAN_GI_400NS : WLAN_GI_800NS);
        CBW cbw = (group_idx % kHtNumCbw == 0 ? CBW20 : CBW40);
        uint8_t mcs_idx = (idx - kHtStartIdx) % kHtNumMcs;

        *tx_vec = TxVector{
            .phy = phy,
            .gi = gi,
            .cbw = cbw,
            .nss = static_cast<uint8_t>(1 + mcs_idx / kHtNumUniqueMcs),
            .mcs_idx = mcs_idx,
        };
        break;
    }
    case WLAN_PHY_ERP:
        *tx_vec = TxVector{
            .phy = phy,
            .gi = WLAN_GI_800NS,
            .cbw = CBW20,
            .nss = 1,
            .mcs_idx = static_cast<uint8_t>(idx - kErpStartIdx),
        };
        break;
    case WLAN_PHY_DSSS:
    case WLAN_PHY_CCK:
        *tx_vec = TxVector{
            .phy = phy,
            .gi = WLAN_GI_800NS,
            .cbw = CBW20,
            .nss = 1,
            .mcs_idx = static_cast<uint8_t>(idx - kDsssCckStartIdx),
        };
        break;
    default:
        // Not reachable.
        ZX_DEBUG_ASSERT(false);
        break;
    }
    return ZX_OK;
}

bool TxVector::IsValid() const {
    if (!(phy == WLAN_PHY_CCK || phy == WLAN_PHY_DSSS || phy == WLAN_PHY_ERP ||
          phy == WLAN_PHY_HT)) {
        return false;
    }
    switch (phy) {
    case WLAN_PHY_DSSS:
        return mcs_idx == 0 || mcs_idx == 1;
    case WLAN_PHY_CCK:
        return mcs_idx == 2 || mcs_idx == 3;
    case WLAN_PHY_HT:
        if (!(gi == WLAN_GI_800NS || gi == WLAN_GI_400NS)) { return false; }
        if (!(cbw == CBW20 || cbw == CBW40 || cbw == CBW40ABOVE || cbw == CBW40BELOW)) {
            return false;
        }
        return 0 <= mcs_idx && mcs_idx < kHtNumMcs;
    case WLAN_PHY_ERP:
        return 0 <= mcs_idx && mcs_idx < kErpNumTxVector;
    case WLAN_PHY_VHT:
        // fall through
        // TODO(NET-1541): GI 800ns, 400ns or 200ns, BW any, MCS 0-9
    default:
        return false;
    }
}

zx_status_t TxVector::ToIdx(tx_vec_idx_t* idx) const {
    if (!IsValid()) { return ZX_ERR_INVALID_ARGS; }
    switch (phy) {
    case WLAN_PHY_HT: {
        uint8_t group_idx = 0;
        if (gi == WLAN_GI_400NS) { group_idx = kHtNumCbw; }
        if (cbw == CBW40 || cbw == CBW40ABOVE || cbw == CBW40BELOW) { group_idx++; }

        *idx = kHtStartIdx + (group_idx * kHtNumMcs) + mcs_idx;
        break;
    }
    case WLAN_PHY_ERP:
        *idx = kErpStartIdx + mcs_idx;
        break;
    case WLAN_PHY_CCK:
    case WLAN_PHY_DSSS:
        *idx = kDsssCckStartIdx + mcs_idx;
        break;
    case WLAN_PHY_VHT:
        // fall-through, will never reach because TxVector is always valid.
        // TODO(NET-1541)
    default:
        break;
    }
    return ZX_OK;
}

bool operator==(const TxVector& lhs, const TxVector& rhs) {
    if (lhs.phy != rhs.phy || lhs.mcs_idx != rhs.mcs_idx) { return false; }
    switch (lhs.phy) {
    case WLAN_PHY_HT:
        return lhs.gi == rhs.gi && lhs.cbw == rhs.cbw;
    case WLAN_PHY_ERP:
    case WLAN_PHY_CCK:
    case WLAN_PHY_DSSS:
        return true;
    default:
        return false;
    }
}

bool operator!=(const TxVector& lhs, const TxVector& rhs) {
    return !(lhs == rhs);
}

bool IsEqualExceptMcs(const ::wlan::TxVector& lhs, const ::wlan::TxVector& rhs) {
    ::wlan::TxVector temp = lhs;
    temp.mcs_idx = rhs.mcs_idx;  // Make mcs_idx equal so that we only compare other fields.
    return rhs == temp;
}
}  // namespace wlan
