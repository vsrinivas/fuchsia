// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/mlme/mac_frame.h>

#include <wlan/mlme/packet.h>

#include <ddk/protocol/wlan.h>
#include <fbl/algorithm.h>

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
        txinfo.data_rate = 12;  // 6 Mbps, one of the basic rates.
        txinfo.mcs = 0x1;       // TODO(porce): Merge data_rate into MCS.
        break;
    }

    (*packet)->CopyCtrlFrom(txinfo);
    return ZX_OK;
}

}  // namespace wlan
