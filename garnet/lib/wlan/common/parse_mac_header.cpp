// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/common/parse_mac_header.h>

namespace wlan {
namespace common {

std::optional<ParsedDataFrameHeader> ParseDataFrameHeader(BufferReader* r) {
    ParsedDataFrameHeader ret = {};

    ret.fixed = r->Read<DataFrameHeader>();
    if (ret.fixed == nullptr) { return {}; }

    if (ret.fixed->fc.to_ds() == 1 && ret.fixed->fc.from_ds() == 1) {
        ret.addr4 = r->Read<MacAddr>();
        if (ret.addr4 == nullptr) { return {}; }
    }

    if ((ret.fixed->fc.subtype() & DataSubtypeBitmask::kBitmaskQos) != 0) {
        ret.qos_ctrl = r->Read<QosControl>();
        if (ret.qos_ctrl == nullptr) { return {}; }
    }

    if (ret.fixed->fc.HasHtCtrl()) {
        ret.ht_ctrl = r->Read<HtControl>();
        if (ret.ht_ctrl == nullptr) { return {}; }
    }

    return {ret};
}

std::optional<ParsedMeshDataHeader> ParseMeshDataHeader(BufferReader* r) {
    auto mac_header = ParseDataFrameHeader(r);
    if (!mac_header) { return {}; }
    if (mac_header->qos_ctrl == nullptr) { return {}; }
    if ((mac_header->qos_ctrl->byte() & QosControl::kMeshControlPresentBit) == 0) { return {}; }

    ParsedMeshDataHeader ret = {};
    ret.mac_header = *mac_header;

    ret.mesh_ctrl = r->Read<MeshControl>();
    if (ret.mesh_ctrl == nullptr) { return {}; }

    size_t num_ext_addr = 0;
    switch (ret.mesh_ctrl->flags.addr_ext_mode()) {
    case kAddrExtNone:
        break;
    case kAddrExt4:
        num_ext_addr = 1;
        break;
    case kAddrExt56:
        num_ext_addr = 2;
        break;
    default:
        return {};
    }

    ret.addr_ext = r->ReadArray<common::MacAddr>(num_ext_addr);
    if (ret.addr_ext.size() != num_ext_addr) { return {}; }

    ret.llc = r->Read<LlcHeader>();
    if (ret.llc == nullptr) { return {}; }

    return {ret};
}

}  // namespace common
}  // namespace wlan
