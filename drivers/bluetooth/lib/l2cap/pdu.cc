// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "pdu.h"

#include "garnet/drivers/bluetooth/lib/hci/acl_data_packet.h"

namespace bluetooth {
namespace l2cap {

PDU::Reader::Reader(const PDU* pdu)
    : offset_(sizeof(BasicHeader)),
      frag_offset_(sizeof(BasicHeader)),
      pdu_(pdu) {
  FXL_DCHECK(pdu_);
  FXL_DCHECK(pdu_->is_valid());

  cur_fragment_ = pdu_->fragments_.cbegin();
}

bool PDU::Reader::ReadNext(size_t size, const ReadFunc& func) {
  FXL_DCHECK(func);

  if (!size)
    return false;

  if (cur_fragment_ == pdu_->fragments_.cend() ||
      offset_ + size > pdu_->length() + sizeof(BasicHeader)) {
    return false;
  }

  // Return a view to avoid copying if the fragment boundary is not being
  // crossed.
  size_t frag_size = cur_fragment_->view().payload_size();
  if (frag_offset_ + size <= frag_size) {
    func(cur_fragment_->view().payload_data().view(frag_offset_, size));

    offset_ += size;
    frag_offset_ += size;
    if (frag_offset_ == frag_size) {
      frag_offset_ = 0u;
      ++cur_fragment_;
    }
    return true;
  }

  // TODO(armansito): This will work fine for small sizes but we'll need to
  // dynamically allocate for packets that are large. Fix this once L2CAP slab
  // allocators have been wired up.
  if (size > 1024) {
    FXL_LOG(WARNING) << "Need to dynamically allocate buffer (size: " << size
                     << ")";
    return false;
  }

  uint8_t buffer[size];
  common::MutableBufferView out(buffer, size);

  size_t remaining = size;
  while (cur_fragment_ != pdu_->fragments_.cend() && remaining) {
    // Calculate how much to copy from the current fragment.
    auto payload = cur_fragment_->view().payload_data();
    size_t copy_size = std::min(payload.size() - frag_offset_, remaining);
    out.Write(payload.data() + frag_offset_, copy_size, size - remaining);

    offset_ += copy_size;
    frag_offset_ += copy_size;
    remaining -= copy_size;

    // Reset fragment offset if we processed an entire fragment.
    FXL_DCHECK(frag_offset_ <= payload.size());
    if (frag_offset_ == payload.size()) {
      frag_offset_ = 0u;
      ++cur_fragment_;
    }
  }

  func(out);
  return true;
}

PDU::PDU() : fragment_count_(0u) {}

// NOTE: The order in which these are initialized matters, as
// other.ReleaseFragments() resets |other.fragment_count_|.
PDU::PDU(PDU&& other)
    : fragment_count_(other.fragment_count_),
      fragments_(other.ReleaseFragments()) {}

PDU& PDU::operator=(PDU&& other) {
  // NOTE: The order in which these are initialized matters, as
  // other.ReleaseFragments() resets |other.fragment_count_|.
  fragment_count_ = other.fragment_count_;
  fragments_ = other.ReleaseFragments();
  return *this;
}

size_t PDU::Copy(common::MutableByteBuffer* out_buffer,
                 size_t pos,
                 size_t size) const {
  FXL_DCHECK(out_buffer);
  FXL_DCHECK(pos < length());
  FXL_DCHECK(is_valid());

  size_t remaining = std::min(size, length() - pos);
  FXL_DCHECK(out_buffer->size() >= remaining);

  bool found = false;
  size_t offset = 0u;
  for (auto iter = fragments_.begin(); iter != fragments_.end() && remaining;
       ++iter) {
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

    // Clear |pos| after using it on the first fragment as all successive
    // fragments are read from the beginning.
    if (pos)
      pos = 0u;

    offset += write_size;
    remaining -= write_size;
  }

  return offset;
}

const common::BufferView PDU::ViewFirstFragment(size_t size) const {
  FXL_DCHECK(is_valid());
  return fragments_.begin()->view().payload_data().view(sizeof(BasicHeader),
                                                        size);
}

PDU::FragmentList PDU::ReleaseFragments() {
  auto out_list = std::move(fragments_);
  fragment_count_ = 0u;

  FXL_DCHECK(!is_valid());
  return out_list;
}

const BasicHeader& PDU::basic_header() const {
  FXL_DCHECK(!fragments_.is_empty());
  const auto& fragment = *fragments_.begin();

  FXL_DCHECK(fragment.packet_boundary_flag() !=
             hci::ACLPacketBoundaryFlag::kContinuingFragment);
  return fragment.view().payload<BasicHeader>();
}

void PDU::AppendFragment(hci::ACLDataPacketPtr fragment) {
  FXL_DCHECK(fragment);
  FXL_DCHECK(!is_valid() || fragments_.begin()->connection_handle() ==
                                fragment->connection_handle());
  fragments_.push_back(std::move(fragment));
  fragment_count_++;
}

}  // namespace l2cap
}  // namespace bluetooth
