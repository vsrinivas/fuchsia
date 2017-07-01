// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "pdu.h"

#include "apps/bluetooth/lib/hci/acl_data_packet.h"

namespace bluetooth {
namespace l2cap {

const BasicHeader& PDU::basic_header() const {
  FTL_DCHECK(!fragments_.is_empty());
  const auto& fragment = *fragments_.begin();

  FTL_DCHECK(fragment.packet_boundary_flag() != hci::ACLPacketBoundaryFlag::kContinuingFragment);
  return fragment.view().payload<BasicHeader>();
}

void PDU::AppendFragment(std::unique_ptr<hci::ACLDataPacket> fragment) {
  FTL_DCHECK(fragment);
  FTL_DCHECK(!is_valid() ||
             fragments_.begin()->connection_handle() == fragment->connection_handle());
  fragments_.push_back(std::move(fragment));
}

size_t PDU::Read(common::MutableByteBuffer* out_buffer, size_t pos, size_t size) const {
  FTL_DCHECK(out_buffer);
  FTL_DCHECK(pos < length());
  FTL_DCHECK(is_valid());

  size_t remaining = std::min(size, length() - pos);
  FTL_DCHECK(out_buffer->size() >= remaining);

  bool found = false;
  size_t offset = 0u;
  for (auto iter = fragments_.begin(); iter != fragments_.end() && remaining; ++iter) {
    auto payload = iter->view().payload_data();

    // Skip the Basic L2CAP header for the first fragment.
    if (iter == fragments_.begin()) {
      payload = payload.view(sizeof(BasicHeader));
    }

    // We first find the beginning fragment based on |pos|.
    if (!found) {
      size_t fragment_size = payload.size();
      if (pos >= fragment_size) {
        pos -= fragment_size;
        continue;
      }

      // The beginning fragment has been found.
      found = true;
    }

    // Calculate how much to read from the current fragment
    size_t write_size = std::min(payload.size() - pos, remaining);
    out_buffer->Write(payload.data() + pos, write_size, offset);

    // Clear |pos| after using it on the first fragment as all successive fragments are read from
    // the beginning.
    if (pos) pos = 0u;

    offset += write_size;
    remaining -= write_size;
  }

  return offset;
}

}  // namespace l2cap
}  // namespace bluetooth
