// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "pdu.h"

#include "apps/bluetooth/lib/hci/acl_data_packet.h"

namespace bluetooth {
namespace l2cap {

PDU::PDU() : fragment_count_(0u) {}

PDU::PDU(PDU&& other) {
  // NOTE: The order in which these are initialized matters, as other.ReleaseFragments() resets
  // |other.fragment_count_|.
  fragment_count_ = other.fragment_count_;
  other.ReleaseFragments(&fragments_);
}

PDU& PDU::operator=(PDU&& other) {
  // NOTE: The order in which these are initialized matters, as other.ReleaseFragments() resets
  // |other.fragment_count_|.
  fragment_count_ = other.fragment_count_;
  other.ReleaseFragments(&fragments_);
  return *this;
}

size_t PDU::Copy(common::MutableByteBuffer* out_buffer, size_t pos, size_t size) const {
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

    // Read the fragment into out_buffer->mutable_data() + offset.
    out_buffer->Write(payload.data() + pos, write_size, offset);

    // Clear |pos| after using it on the first fragment as all successive fragments are read from
    // the beginning.
    if (pos) pos = 0u;

    offset += write_size;
    remaining -= write_size;
  }

  return offset;
}

const common::BufferView PDU::ViewFirstFragment(size_t size) const {
  FTL_DCHECK(is_valid());
  return fragments_.begin()->view().payload_data().view(sizeof(BasicHeader), size);
}

void PDU::ReleaseFragments(FragmentList* out_list) {
  FTL_DCHECK(out_list);

  *out_list = std::move(fragments_);
  fragment_count_ = 0u;

  FTL_DCHECK(!is_valid());
}

const BasicHeader& PDU::basic_header() const {
  FTL_DCHECK(!fragments_.is_empty());
  const auto& fragment = *fragments_.begin();

  FTL_DCHECK(fragment.packet_boundary_flag() != hci::ACLPacketBoundaryFlag::kContinuingFragment);
  return fragment.view().payload<BasicHeader>();
}

void PDU::AppendFragment(hci::ACLDataPacketPtr fragment) {
  FTL_DCHECK(fragment);
  FTL_DCHECK(!is_valid() ||
             fragments_.begin()->connection_handle() == fragment->connection_handle());
  fragments_.push_back(std::move(fragment));
  fragment_count_++;
}

}  // namespace l2cap
}  // namespace bluetooth
