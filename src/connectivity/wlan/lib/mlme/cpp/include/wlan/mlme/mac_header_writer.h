// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_INCLUDE_WLAN_MLME_MAC_HEADER_WRITER_H_
#define SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_INCLUDE_WLAN_MLME_MAC_HEADER_WRITER_H_

#include <wlan/common/buffer_writer.h>
#include <wlan/common/mac_frame.h>
#include <wlan/common/macaddr.h>
#include <wlan/mlme/rust_utils.h>

namespace wlan {

// Note that the current implementation might not fill out all the fields.
// Please examine the code in *.cpp before using it for your purpose.
class MacHeaderWriter {
 public:
  explicit MacHeaderWriter(const common::MacAddr& transmitter_addr,
                           mlme_sequence_manager_t* seq_mgr)
      : transmitter_addr_(transmitter_addr), seq_mgr_(seq_mgr) {}

  void WriteMeshMgmtHeader(BufferWriter* writer, ManagementSubtype subtype,
                           const common::MacAddr& dst_addr) const;

  void WriteMeshDataHeaderIndivAddressed(
      BufferWriter* w, const common::MacAddr& next_hop_addr,
      const common::MacAddr& mesh_dst_addr,
      const common::MacAddr& mesh_src_addr) const;

  void WriteMeshDataHeaderGroupAddressed(
      BufferWriter* w, const common::MacAddr& dst_addr,
      const common::MacAddr& mesh_src_addr) const;

 private:
  common::MacAddr transmitter_addr_;
  mlme_sequence_manager_t* seq_mgr_;
};

}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_INCLUDE_WLAN_MLME_MAC_HEADER_WRITER_H_
