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
    case DataSubtype::kQosdata: {
        auto llc_frame = data_frame.Specialize<LlcHeader>();
        if (!llc_frame.HasValidLen()) {
            errorf("short data packet len=%zu\n", llc_frame.len());
            return ZX_ERR_IO;
        }
        return target->HandleFrame(llc_frame);
    }
    default:
        // No support of PCF / HCCA
        return ZX_OK;
    }
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
        return target->HandleFrame(frame);
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
