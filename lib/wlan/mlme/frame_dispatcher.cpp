// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/mlme/frame_dispatcher.h>

#include <wlan/common/mac_frame.h>
#include <wlan/mlme/debug.h>
#include <wlan/protocol/mac.h>

#include <cinttypes>
#include <cstring>

namespace wlan {

namespace {

zx_status_t HandleCtrlPacket(fbl::unique_ptr<Packet> packet, FrameHandler* target) {
    debugfn();
    ZX_DEBUG_ASSERT(packet->has_ctrl_data<wlan_rx_info_t>());
    if (!packet->has_ctrl_data<wlan_rx_info_t>()) {
        errorf("MAC frame should carry wlan_rx_info\n");
        return ZX_ERR_INVALID_ARGS;
    }

    Frame<FrameControl> ctrl_frame(fbl::move(packet));
    if (!ctrl_frame.HasValidLen()) { return ZX_ERR_IO; }

    auto fc = ctrl_frame.hdr();
    switch (fc->subtype()) {
    case ControlSubtype::kPsPoll: {
        CtrlFrame<PsPollFrame> ps_poll(ctrl_frame.Take());
        if (!ps_poll.HasValidLen()) {
            errorf("short ps poll frame len=%zu\n", ps_poll.len());
            return ZX_ERR_IO;
        }
        return target->HandleFrame(ps_poll);
    }
    default:
        debugf("unsupported ctrl subtype 0x%02x\n", fc->subtype());
        return ZX_OK;
    }
}

zx_status_t HandleDataPacket(fbl::unique_ptr<Packet> packet, FrameHandler* target) {
    debugfn();
    ZX_DEBUG_ASSERT(packet->has_ctrl_data<wlan_rx_info_t>());
    if (!packet->has_ctrl_data<wlan_rx_info_t>()) {
        errorf("MAC frame should carry wlan_rx_info\n");
        return ZX_ERR_INVALID_ARGS;
    }

    DataFrame<UnknownBody> data_frame(fbl::move(packet));
    if (!data_frame.HasValidLen()) { return ZX_ERR_IO; }

    auto hdr = data_frame.hdr();
    switch (hdr->fc.subtype()) {
    case DataSubtype::kNull:
        // Fall-through
    case DataSubtype::kQosnull: {
        auto null_frame = data_frame.Specialize<NilHeader>();
        return target->HandleFrame(null_frame);
    }
    case DataSubtype::kDataSubtype:
        // Fall-through
    case DataSubtype::kQosdata:
        break;
    default:
        warnf("unsupported data subtype %02x\n", hdr->fc.subtype());
        return ZX_OK;
    }

    auto llc_frame = data_frame.Specialize<LlcHeader>();
    if (!llc_frame.HasValidLen()) {
        errorf("short data packet len=%zu\n", llc_frame.len());
        return ZX_ERR_IO;
    }
    return target->HandleFrame(llc_frame);
}

zx_status_t HandleActionPacket(MgmtFrame<ActionFrame> action_frame, FrameHandler* target) {
    if (action_frame.body()->category != action::Category::kBlockAck) {
        verbosef("Rxed Action frame with category %d. Not handled.\n",
                 action_frame.body()->category);
        return ZX_OK;
    }

    auto ba_frame = action_frame.Specialize<ActionFrameBlockAck>();
    if (!ba_frame.HasValidLen()) {
        errorf("bloackack packet too small (len=%zd)\n", ba_frame.len());
        return ZX_ERR_IO;
    }

    switch (ba_frame.body()->action) {
    case action::BaAction::kAddBaRequest: {
        auto addbar = ba_frame.Specialize<AddBaRequestFrame>();
        if (!addbar.HasValidLen()) {
            errorf("addbar packet too small (len=%zd)\n", addbar.len());
            return ZX_ERR_IO;
        }

        // TODO(porce): Support AddBar. Work with lower mac.
        // TODO(porce): Make this conditional depending on the hardware capability.

        return target->HandleFrame(addbar);
    }
    case action::BaAction::kAddBaResponse: {
        auto addba_resp = ba_frame.Specialize<AddBaResponseFrame>();
        if (!addba_resp.HasValidLen()) {
            errorf("addba_resp packet too small (len=%zd)\n", addba_resp.len());
            return ZX_ERR_IO;
        }
        return target->HandleFrame(addba_resp);
    }
    case action::BaAction::kDelBa:
    // fall-through
    default:
        warnf("BlockAck action frame with action %u not handled.\n", ba_frame.body()->action);
        break;
    }
    return ZX_OK;
}

zx_status_t HandleMgmtPacket(fbl::unique_ptr<Packet> packet, FrameHandler* target) {
    debugfn();
    ZX_DEBUG_ASSERT(packet->has_ctrl_data<wlan_rx_info_t>());
    if (!packet->has_ctrl_data<wlan_rx_info_t>()) {
        errorf("MAC frame should carry wlan_rx_info\n");
        return ZX_ERR_INVALID_ARGS;
    }

    MgmtFrame<UnknownBody> mgmt_frame(fbl::move(packet));
    if (!mgmt_frame.HasValidLen()) { return ZX_ERR_IO; }

    auto hdr = mgmt_frame.hdr();
    debughdr("Frame control: %04x  duration: %u  seq: %u frag: %u\n", hdr->fc.val(), hdr->duration,
             hdr->sc.seq(), hdr->sc.frag());

    const common::MacAddr& dst = hdr->addr1;
    const common::MacAddr& src = hdr->addr2;
    const common::MacAddr& bssid = hdr->addr3;

    debughdr("dest: %s source: %s bssid: %s\n", MACSTR(dst), MACSTR(src), MACSTR(bssid));

    switch (hdr->fc.subtype()) {
    case ManagementSubtype::kBeacon: {
        auto frame = mgmt_frame.Specialize<Beacon>();
        if (!frame.HasValidLen()) {
            errorf("beacon packet too small (len=%zd)\n", frame.len());
            return ZX_ERR_IO;
        }
        return target->HandleFrame(frame);
    }
    case ManagementSubtype::kProbeResponse: {
        auto frame = mgmt_frame.Specialize<ProbeResponse>();
        if (!frame.HasValidLen()) {
            errorf("probe response packet too small (len=%zd)\n", frame.len());
            return ZX_ERR_IO;
        }
        return target->HandleFrame(frame);
    }
    case ManagementSubtype::kProbeRequest: {
        auto frame = mgmt_frame.Specialize<ProbeRequest>();
        if (!frame.HasValidLen()) {
            errorf("probe request packet too small (len=%zd)\n", frame.len());
            return ZX_ERR_IO;
        }
        return target->HandleFrame(frame);
    }
    case ManagementSubtype::kAuthentication: {
        auto frame = mgmt_frame.Specialize<Authentication>();
        if (!frame.HasValidLen()) {
            errorf("authentication packet too small (len=%zd)\n", frame.len());
            return ZX_ERR_IO;
        }
        return target->HandleFrame(frame);
    }
    case ManagementSubtype::kDeauthentication: {
        auto frame = mgmt_frame.Specialize<Deauthentication>();
        if (!frame.HasValidLen()) {
            errorf("deauthentication packet too small (len=%zd)\n", frame.len());
            return ZX_ERR_IO;
        }
        return target->HandleFrame(frame);
    }
    case ManagementSubtype::kAssociationRequest: {
        auto frame = mgmt_frame.Specialize<AssociationRequest>();
        if (!frame.HasValidLen()) {
            errorf("assocation request packet too small (len=%zd)\n", frame.len());
            return ZX_ERR_IO;
        }
        return target->HandleFrame(frame);
    }
    case ManagementSubtype::kAssociationResponse: {
        auto frame = mgmt_frame.Specialize<AssociationResponse>();
        if (!frame.HasValidLen()) {
            errorf("assocation response packet too small (len=%zd)\n", frame.len());
            return ZX_ERR_IO;
        }
        return target->HandleFrame(frame);
    }
    case ManagementSubtype::kDisassociation: {
        auto frame = mgmt_frame.Specialize<Disassociation>();
        if (!frame.HasValidLen()) {
            errorf("disassociation packet too small (len=%zd)\n", frame.len());
            return ZX_ERR_IO;
        }
        return target->HandleFrame(frame);
    }
    case ManagementSubtype::kAction: {
        auto frame = mgmt_frame.Specialize<ActionFrame>();
        if (!frame.HasValidLen()) {
            errorf("action packet too small (len=%zd)\n", frame.len());
            return ZX_ERR_IO;
        }
        if (!frame.hdr()->IsAction()) {
            errorf("action packet is not an action\n");
            return ZX_ERR_IO;
        }
        HandleActionPacket(fbl::move(frame), target);
    }
    default:
        if (!dst.IsBcast()) {
            // TODO(porce): Evolve this logic to support AP role.
            debugf("Rxed Mgmt frame (type: %d) but not handled\n", hdr->fc.subtype());
        }
        break;
    }
    return ZX_OK;
}

zx_status_t HandleEthPacket(fbl::unique_ptr<Packet> packet, FrameHandler* target) {
    debugfn();

    EthFrame eth_frame(fbl::move(packet));
    if (!eth_frame.HasValidLen()) {
        errorf("short ethernet frame len=%zu\n", eth_frame.len());
        return ZX_ERR_IO;
    }
    return target->HandleFrame(eth_frame);
}

}  // namespace

zx_status_t DispatchMlmeMsg(const BaseMlmeMsg& msg, FrameHandler* target) {
    ZX_DEBUG_ASSERT(target != nullptr);
    if (target == nullptr) { return ZX_ERR_INVALID_ARGS; }

    if (auto reset_req = msg.As<wlan_mlme::ResetRequest>()) {
        target->HandleFrame(*reset_req);
    } else if (auto start_req = msg.As<wlan_mlme::StartRequest>()) {
        target->HandleFrame(*start_req);
    } else if (auto stop_req = msg.As<wlan_mlme::StopRequest>()) {
        target->HandleFrame(*stop_req);
    } else if (auto scan_req = msg.As<wlan_mlme::ScanRequest>()) {
        target->HandleFrame(*scan_req);
    } else if (auto join_req = msg.As<wlan_mlme::JoinRequest>()) {
        target->HandleFrame(*join_req);
    } else if (auto auth_req = msg.As<wlan_mlme::AuthenticateRequest>()) {
        target->HandleFrame(*auth_req);
    } else if (auto auth_resp = msg.As<wlan_mlme::AuthenticateResponse>()) {
        target->HandleFrame(*auth_resp);
    } else if (auto deauth_req = msg.As<wlan_mlme::DeauthenticateRequest>()) {
        target->HandleFrame(*deauth_req);
    } else if (auto assoc_req = msg.As<wlan_mlme::AssociateRequest>()) {
        target->HandleFrame(*assoc_req);
    } else if (auto assoc_resp = msg.As<wlan_mlme::AssociateResponse>()) {
        target->HandleFrame(*assoc_resp);
    } else if (auto eapol_req = msg.As<wlan_mlme::EapolRequest>()) {
        target->HandleFrame(*eapol_req);
    } else if (auto setkeys_req = msg.As<wlan_mlme::SetKeysRequest>()) {
        target->HandleFrame(*setkeys_req);
    } else {
        ZX_DEBUG_ASSERT(false);
    }
    return ZX_OK;
}

zx_status_t DispatchFramePacket(fbl::unique_ptr<Packet> packet, FrameHandler* target) {
    debugfn();
    ZX_DEBUG_ASSERT(packet != nullptr);
    ZX_DEBUG_ASSERT(target != nullptr);
    if (packet == nullptr || target == nullptr) { return ZX_ERR_INVALID_ARGS; }

    switch (packet->peer()) {
    case Packet::Peer::kEthernet:
        return HandleEthPacket(fbl::move(packet), target);
    case Packet::Peer::kWlan: {
        auto fc = packet->field<FrameControl>(0);

        // TODO(porce): Handle HTC field.
        if (fc->HasHtCtrl()) {
            warnf("WLAN frame (type %u:%u) HTC field is present but not handled. Drop.", fc->type(),
                  fc->subtype());
            return ZX_ERR_NOT_SUPPORTED;
        }

        switch (fc->type()) {
        case FrameType::kManagement:
            return HandleMgmtPacket(fbl::move(packet), target);
        case FrameType::kControl:
            return HandleCtrlPacket(fbl::move(packet), target);
        case FrameType::kData:
            return HandleDataPacket(fbl::move(packet), target);
        default:
            warnf("unknown MAC frame type %u\n", fc->type());
            return ZX_ERR_NOT_SUPPORTED;
        }
    }
    default:
        // Unsupported Packet type.
        ZX_DEBUG_ASSERT(false);
        return ZX_ERR_NOT_SUPPORTED;
    }
}

}  // namespace wlan
