// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/mlme/mesh/write_mp_action.h>

#include <wlan/common/write_element.h>
#include <wlan/mlme/rates_elements.h>

namespace wlan_mlme = ::fuchsia::wlan::mlme;

namespace wlan {

static void WriteFixed(BufferWriter* w, const MacHeaderWriter& mac_header_writer,
                       const common::MacAddr& dst_addr, action::SelfProtectedAction action) {
    // Mac header
    mac_header_writer.WriteMeshMgmtHeader(w, kAction, dst_addr);

    // Action header
    w->Write<ActionFrame>()->category = action::kSelfProtected;
    w->Write<SelfProtectedActionHeader>()->self_prot_action = action;

    // Capability info: leave ESS and IBSS set to zero to indicate 'mesh'.
    // Hardcode short preamble because the rest of our code does so...
    auto cap_info = w->Write<CapabilityInfo>();
    cap_info->set_short_preamble(1);
}

static void WriteCommonElementsHead(BufferWriter* w, const wlan_mlme::MeshPeeringCommon& c) {
    RatesWriter rates_writer({
        reinterpret_cast<const SupportedRate*>(c.rates.data()),
        c.rates.size(),
    });
    rates_writer.WriteSupportedRates(w);
    rates_writer.WriteExtendedSupportedRates(w);

    common::WriteMeshId(w, c.mesh_id);
    common::WriteMeshConfiguration(w, MeshConfiguration::FromFidl(c.mesh_config));
}

static void WriteCommonElementsTail(BufferWriter* w, const wlan_mlme::MeshPeeringCommon& c) {
    if (c.ht_cap != nullptr) {
        common::WriteHtCapabilities(w, HtCapabilities::FromFidl(*c.ht_cap));
    }
    if (c.ht_op != nullptr) { common::WriteHtOperation(w, HtOperation::FromFidl(*c.ht_op)); }
    if (c.vht_cap != nullptr) {
        common::WriteVhtCapabilities(w, VhtCapabilities::FromFidl(*c.vht_cap));
    }
    if (c.vht_op != nullptr) { common::WriteVhtOperation(w, VhtOperation::FromFidl(*c.vht_op)); }
}

void WriteMpOpenActionFrame(BufferWriter* w, const MacHeaderWriter& mac_header_writer,
                            const wlan_mlme::MeshPeeringOpenAction& action) {
    common::MacAddr dst_addr{action.common.peer_sta_address};
    WriteFixed(w, mac_header_writer, dst_addr, action::kMeshPeeringOpen);
    WriteCommonElementsHead(w, action.common);

    MpmHeader mpm_header = {
        .protocol = static_cast<MpmHeader::Protocol>(action.common.protocol_id),
        .local_link_id = action.common.local_link_id,
    };
    common::WriteMpmOpen(w, mpm_header, nullptr);

    WriteCommonElementsTail(w, action.common);
}

void WriteMpConfirmActionFrame(BufferWriter* w, const MacHeaderWriter& mac_header_writer,
                               const wlan_mlme::MeshPeeringConfirmAction& action) {
    common::MacAddr dst_addr{action.common.peer_sta_address};
    WriteFixed(w, mac_header_writer, dst_addr, action::kMeshPeeringConfirm);
    w->WriteValue<uint16_t>(action.aid);
    WriteCommonElementsHead(w, action.common);

    MpmHeader mpm_header = {
        .protocol = static_cast<MpmHeader::Protocol>(action.common.protocol_id),
        .local_link_id = action.common.local_link_id,
    };
    common::WriteMpmConfirm(w, mpm_header, action.peer_link_id, nullptr);

    WriteCommonElementsTail(w, action.common);
}

}  // namespace wlan
