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
    mgmt_hdr->addr2 = src_addr_;
    // See IEEE Std 802.11-2016, 9.3.3.2, item "c) 4) iv)": addr3 is set to SA for mesh stations
    mgmt_hdr->addr3 = src_addr_;
    SetSeqNo(mgmt_hdr, seq_);
}

} // namespace wlan
