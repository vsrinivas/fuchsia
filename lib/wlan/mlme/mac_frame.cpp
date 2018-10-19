// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/mlme/mac_frame.h>

#include <wlan/mlme/debug.h>
#include <wlan/mlme/packet.h>

#include <fbl/algorithm.h>
#include <wlan/protocol/mac.h>

namespace wlan {

// TODO(hahnr): Decomission and replace with FrameWriter.
template <typename Body>
zx_status_t CreateMgmtFrame(MgmtFrame<Body>* out_frame, size_t body_payload_len, bool has_ht_ctrl) {
    size_t max_frame_len = MgmtFrameHeader::max_len() + Body::max_len() + body_payload_len;
    auto packet = GetWlanPacket(max_frame_len);
    if (packet == nullptr) { return ZX_ERR_NO_RESOURCES; }
    // Zero out the packet buffer by default for the management frame.
    packet->clear();

    MgmtFrame<Body> frame(fbl::move(packet));
    if (!frame.HasValidLen()) { return ZX_ERR_BUFFER_TOO_SMALL; }

    frame.hdr()->fc.set_type(FrameType::kManagement);
    frame.hdr()->fc.set_subtype(Body::Subtype());
    if (has_ht_ctrl) { frame.hdr()->fc.set_htc_order(1); }

    *out_frame = fbl::move(frame);
    return ZX_OK;
}

#define DECLARE_BUILD_MGMTFRAME(bodytype)                                                    \
    template zx_status_t CreateMgmtFrame(MgmtFrame<bodytype>* frame, size_t reserved_ie_len, \
                                         bool has_ht_ctrl)

DECLARE_BUILD_MGMTFRAME(ProbeRequest);
DECLARE_BUILD_MGMTFRAME(ProbeResponse);
DECLARE_BUILD_MGMTFRAME(Beacon);
DECLARE_BUILD_MGMTFRAME(Authentication);
DECLARE_BUILD_MGMTFRAME(Deauthentication);
DECLARE_BUILD_MGMTFRAME(AssociationRequest);
DECLARE_BUILD_MGMTFRAME(AssociationResponse);
DECLARE_BUILD_MGMTFRAME(Disassociation);
DECLARE_BUILD_MGMTFRAME(ActionFrame);

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

zx_status_t DeaggregateAmsdu(const DataFrameView<AmsduSubframeHeader>& data_amsdu_frame,
                             MsduCallback cb) {
    auto amsdu_subframe = data_amsdu_frame.SkipHeader();
    while (amsdu_subframe) {
        finspect("amsdu subframe: %s\n", debug::Describe(*amsdu_subframe.hdr()).c_str());
        finspect("amsdu subframe dump: %s\n",
                 debug::HexDump(amsdu_subframe.data(), amsdu_subframe.len()).c_str());

        // Note: msdu_len == 0 is valid
        size_t msdu_len = amsdu_subframe.hdr()->msdu_len();
        if (msdu_len > 0) {
            if (auto llc_frame =
                    amsdu_subframe.CheckBodyType<LlcHeader>().CheckLength().SkipHeader()) {
                size_t payload_len = msdu_len - llc_frame.hdr()->len();
                cb(llc_frame, payload_len);
            } else {
                errorf("malformed A-MSDU subframe: amsdu_len %zu, msdu_len %zu\n",
                       amsdu_subframe.len(), msdu_len);
                return ZX_ERR_IO;
            }
        }

        // Advance to next AMSDU subframe by skipping AMSDU header, MSDU and an optional padding.
        size_t base_len = amsdu_subframe.hdr()->len() + msdu_len;
        size_t padded_len = fbl::round_up(base_len, 4u);
        amsdu_subframe =
            amsdu_subframe.AdvanceBy(padded_len).As<AmsduSubframeHeader>().CheckLength();
    }

    return ZX_OK;
}

}  // namespace wlan
