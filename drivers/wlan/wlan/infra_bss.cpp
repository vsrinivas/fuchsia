// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "infra_bss.h"

namespace wlan {

bool InfraBss::ShouldDropMgmtFrame(const MgmtFrameHeader& hdr) {
    // Drop management frames which are not targeted towards this BSS.
    return bssid_ != hdr.addr1 || bssid_ != hdr.addr3;
}

const common::MacAddr& InfraBss::bssid() {
    return bssid_;
}

uint16_t InfraBss::next_seq_no() {
    return last_seq_no_++ & kMaxSequenceNumber;
}

uint64_t InfraBss::timestamp() {
    bss::timestamp_t now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(now - started_at_).count();
}

}  // namespace wlan