// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/mlme/mac_frame.h>

#include <wlan/mlme/packet.h>

#include <fbl/algorithm.h>
#include <wlan/protocol/mac.h>

namespace wlan {

// TODO(porce): Consider zx_status_t return type
template <typename Body>
MgmtFrame<Body> BuildMgmtFrame(fbl::unique_ptr<Packet>* packet, size_t body_payload_len,
                               bool has_ht_ctrl) {
    size_t hdr_len = sizeof(MgmtFrameHeader) + (has_ht_ctrl ? kHtCtrlLen : 0);
    size_t body_len = sizeof(Body) + body_payload_len;
    size_t frame_len = hdr_len + body_len;

    *packet = Packet::CreateWlanPacket(frame_len);
    if (*packet == nullptr) { return MgmtFrame<Body>(nullptr, nullptr, 0); }

    // Zero out the packet buffer by default for the management frame.
    (*packet)->clear();

    auto hdr = (*packet)->mut_field<MgmtFrameHeader>(0);
    hdr->fc.set_subtype(Body::Subtype());
    if (has_ht_ctrl) { hdr->fc.set_htc_order(1); }

    auto body = (*packet)->mut_field<Body>(hdr->len());
    return MgmtFrame<Body>(hdr, body, body_len);
}

#define DECLARE_BUILD_MGMTFRAME(bodytype)                                        \
    template MgmtFrame<bodytype> BuildMgmtFrame(fbl::unique_ptr<Packet>* packet, \
                                                size_t body_payload_len, bool has_ht_ctrl)

DECLARE_BUILD_MGMTFRAME(ProbeRequest);
DECLARE_BUILD_MGMTFRAME(ProbeResponse);
DECLARE_BUILD_MGMTFRAME(Beacon);
DECLARE_BUILD_MGMTFRAME(Authentication);
DECLARE_BUILD_MGMTFRAME(Deauthentication);
DECLARE_BUILD_MGMTFRAME(AssociationRequest);
DECLARE_BUILD_MGMTFRAME(AssociationResponse);
DECLARE_BUILD_MGMTFRAME(Disassociation);
DECLARE_BUILD_MGMTFRAME(AddBaRequestFrame);
DECLARE_BUILD_MGMTFRAME(AddBaResponseFrame);

zx_status_t FillTxInfo(fbl::unique_ptr<Packet>* packet, const MgmtFrameHeader& hdr) {
    // TODO(porce): Evolve the API to use FrameHeader
    // and support all types of frames.
    ZX_DEBUG_ASSERT(packet != nullptr && *packet);

    wlan_tx_info_t txinfo = {
        // Outgoing management frame
        .tx_flags = 0x0,
        .valid_fields = WLAN_TX_INFO_VALID_PHY | WLAN_TX_INFO_VALID_CHAN_WIDTH,
        .phy = WLAN_PHY_OFDM,  // Always
        .cbw = CBW20,          // Use CBW20 always
    };

    // TODO(porce): Imeplement rate selection.
    switch (hdr.fc.subtype()) {
    default:
        txinfo.valid_fields |= WLAN_TX_INFO_VALID_MCS;
        // txinfo.data_rate = 12;  // 6 Mbps, one of the basic rates.
        txinfo.mcs = 0x3;  // TODO(NET-645): Choose an optimal MCS
        break;
    }

    (*packet)->CopyCtrlFrom(txinfo);
    return ZX_OK;
}

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

}  // namespace wlan
