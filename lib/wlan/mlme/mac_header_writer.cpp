// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/mlme/mac_header_writer.h>

#include <wlan/mlme/mac_frame.h>

namespace wlan {

void MacHeaderWriter::WriteMeshMgmtHeader(BufferWriter* w,
                                          ManagementSubtype subtype,
                                          const common::MacAddr& dst_addr) const {
    auto mgmt_hdr = w->Write<MgmtFrameHeader>();
    mgmt_hdr->fc.set_type(FrameType::kManagement);
    mgmt_hdr->fc.set_subtype(subtype);
    mgmt_hdr->fc.set_from_ds(0);
    mgmt_hdr->fc.set_to_ds(0);
    mgmt_hdr->addr1 = dst_addr;
    mgmt_hdr->addr2 = transmitter_addr_;
    // See IEEE Std 802.11-2016, 9.3.3.2, item "c) 4) iv)": addr3 is set to SA for mesh stations
    mgmt_hdr->addr3 = transmitter_addr_;
    SetSeqNo(mgmt_hdr, seq_);
}

// See IEEE Std 802.11-2016, 9.3.5 (Table 9-42). See also 10.35.3.
void MacHeaderWriter::WriteMeshDataHeaderIndivAddressed(
       BufferWriter* w,
       const common::MacAddr& next_hop_addr,
       const common::MacAddr& mesh_dst_addr,
       const common::MacAddr& mesh_src_addr) const
{
    auto data_hdr = w->Write<DataFrameHeader>();
    data_hdr->fc.set_type(FrameType::kData);
    data_hdr->fc.set_subtype(DataSubtype::kQosdata);
    data_hdr->fc.set_to_ds(1);
    data_hdr->fc.set_from_ds(1);
    data_hdr->fc.set_htc_order(0);
    data_hdr->fc.set_protected_frame(0);
    data_hdr->addr1 = next_hop_addr;
    data_hdr->addr2 = transmitter_addr_;
    data_hdr->addr3 = mesh_dst_addr;
    SetSeqNo(data_hdr, seq_);

    w->WriteValue(mesh_src_addr); // addr4

    auto qos_ctrl = w->Write<QosControl>();
    qos_ctrl->set_byte(1); // mesh control present
}

// See IEEE Std 802.11-2016, 9.3.5 (Table 9-42). See also 10.35.4.
void MacHeaderWriter::WriteMeshDataHeaderGroupAddressed(
        BufferWriter* w,
        const common::MacAddr& dst_addr,
        const common::MacAddr& mesh_src_addr) const
{
    auto data_hdr = w->Write<DataFrameHeader>();
    data_hdr->fc.set_type(FrameType::kData);
    data_hdr->fc.set_subtype(DataSubtype::kQosdata);
    data_hdr->fc.set_to_ds(0);
    data_hdr->fc.set_from_ds(1);
    data_hdr->fc.set_htc_order(0);
    data_hdr->fc.set_protected_frame(0);
    data_hdr->addr1 = dst_addr;
    data_hdr->addr2 = transmitter_addr_;
    data_hdr->addr3 = mesh_src_addr;
    SetSeqNo(data_hdr, seq_);

    auto qos_ctrl = w->Write<QosControl>();
    qos_ctrl->set_byte(1); // mesh control present
}

} // namespace wlan
