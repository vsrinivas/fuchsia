// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "minstrel.h"

#include <random>

namespace wlan {
void AddHtRates(RateGroup* group, const SupportedMcsRxMcsHead& mcs_set) {
}

MinstrelRateSelector::MinstrelRateSelector(fbl::unique_ptr<wlan::Timer> timer) : timer_(fbl::move(timer)) {
}

void MinstrelRateSelector::AddPeer(common::MacAddr, const HtCapabilities& ht_cap) {
}

void MinstrelRateSelector::RemovePeer(const common::MacAddr& addr) {
}

void MinstrelRateSelector::HandleTxStatusReport(const wlan_tx_status_t& tx_status) {
}

void MinstrelRateSelector::UpdateStats() {
}

Peer* MinstrelRateSelector::GetPeer(const common::MacAddr& addr) {
    return nullptr;
}
}  // namespace wlan
