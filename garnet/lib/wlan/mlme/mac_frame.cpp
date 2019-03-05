// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/mlme/mac_frame.h>

#include <wlan/mlme/debug.h>
#include <wlan/mlme/packet.h>

#include <fbl/algorithm.h>
#include <wlan/protocol/mac.h>

namespace wlan {

// IEEE Std 802.11-2016, 10.3.2.11.2 Table 10-3 SNS1
seq_t NextSeqNo(const MgmtFrameHeader& hdr, Sequence* seq) {
    ZX_DEBUG_ASSERT(seq != nullptr);
    // MMPDU, non-QMF frames
    // TODO(porce): Confirm if broadcast / multicast needs to follow this rule.
    auto& receiver_addr = hdr.addr1;
    return seq->Sns1(receiver_addr)->Next();
}

// IEEE Std 802.11-2016, 10.3.2.11.2 Table 10-3 SNS4
// IEEE Std 802.11ae-2012, 8.2.4.4.2
seq_t NextSeqNo(const MgmtFrameHeader& hdr, uint8_t aci, Sequence* seq) {
    ZX_DEBUG_ASSERT(seq != nullptr);
    ZX_DEBUG_ASSERT(aci < 4);
    // MMPDU, QMF frames
    auto& receiver_addr = hdr.addr1;
    return seq->Sns4(receiver_addr, aci)->Next();
}

// IEEE Std 802.11-2016, 10.3.2.11.2 Table 10-3 SNS2, SNS5
seq_t NextSeqNo(const DataFrameHeader& hdr, Sequence* seq) {
    ZX_DEBUG_ASSERT(seq != nullptr);
    if (!hdr.HasQosCtrl()) { return seq->Sns1(hdr.addr1)->Next(); }
    if (hdr.fc.subtype() == kQosnull) { return seq->Sns5()->Next(); }

    auto qos_ctrl = hdr.qos_ctrl();
    ZX_DEBUG_ASSERT(qos_ctrl != nullptr);

    uint8_t tid = qos_ctrl->tid();
    return seq->Sns2(hdr.addr1, tid)->Next();
}

void SetSeqNo(MgmtFrameHeader* hdr, Sequence* seq) {
    ZX_DEBUG_ASSERT(hdr != nullptr && seq != nullptr);
    seq_t seq_no = NextSeqNo(*hdr, seq);
    hdr->sc.set_seq(seq_no);
}

void SetSeqNo(MgmtFrameHeader* hdr, uint8_t aci, Sequence* seq) {
    ZX_DEBUG_ASSERT(hdr != nullptr && seq != nullptr);
    seq_t seq_no = NextSeqNo(*hdr, aci, seq);
    // seq_no is a pure number, and does not mandate a particular field structure.
    // IEEE Std 802.11-2016, 9.2.4.4.2
    // defines the modified sequence control field structure.
    // This bitshifting accommodates it
    constexpr uint8_t kAciBitLen = 2;
    hdr->sc.set_seq((seq_no << kAciBitLen) + aci);
}

void SetSeqNo(DataFrameHeader* hdr, Sequence* seq) {
    ZX_DEBUG_ASSERT(hdr != nullptr && seq != nullptr);
    seq_t seq_no = NextSeqNo(*hdr, seq);
    hdr->sc.set_seq(seq_no);
}

void FillEtherLlcHeader(LlcHeader* llc, uint16_t protocol_id) {
    llc->dsap = kLlcSnapExtension;
    llc->ssap = kLlcSnapExtension;
    llc->control = kLlcUnnumberedInformation;
    std::memcpy(llc->oui, kLlcOui, sizeof(llc->oui));
    llc->protocol_id = protocol_id;
}

}  // namespace wlan
