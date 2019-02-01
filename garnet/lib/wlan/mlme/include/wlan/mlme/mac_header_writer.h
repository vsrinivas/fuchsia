// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <wlan/common/buffer_writer.h>
#include <wlan/common/macaddr.h>
#include <wlan/common/mac_frame.h>
#include <wlan/mlme/sequence.h>

namespace wlan {

// Note that the current implementation might not fill out all the fields.
// Please examine the code in *.cpp before using it for your purpose.
class MacHeaderWriter {
 public:
    explicit MacHeaderWriter(const common::MacAddr& transmitter_addr, Sequence* seq)
       : transmitter_addr_(transmitter_addr), seq_(seq) { }

    void WriteMeshMgmtHeader(BufferWriter* writer,
                             ManagementSubtype subtype,
                             const common::MacAddr& dst_addr) const;

    void WriteMeshDataHeaderIndivAddressed(BufferWriter* w,
                                           const common::MacAddr& next_hop_addr,
                                           const common::MacAddr& mesh_dst_addr,
                                           const common::MacAddr& mesh_src_addr) const;

    void WriteMeshDataHeaderGroupAddressed(BufferWriter* w,
                                           const common::MacAddr& dst_addr,
                                           const common::MacAddr& mesh_src_addr) const;
 private:
    common::MacAddr transmitter_addr_;
    Sequence* seq_;
};

} // namespace wlan
