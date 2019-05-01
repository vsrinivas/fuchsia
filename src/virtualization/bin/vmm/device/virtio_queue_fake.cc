// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/device/virtio_queue_fake.h"

static size_t desc_size(uint16_t queue_size) {
  return sizeof(*VirtioRing::desc) * queue_size;
}

static size_t avail_size(uint16_t queue_size) {
  return sizeof(*VirtioRing::avail) +
         (sizeof(*vring_avail::ring) * queue_size) +
         sizeof(*VirtioRing::used_event);
}

static size_t used_size(uint16_t queue_size) {
  return sizeof(*VirtioRing::used) + (sizeof(*vring_used::ring) * queue_size) +
         sizeof(*VirtioRing::avail_event);
}

VirtioQueueFake::VirtioQueueFake(const PhysMem& phys_mem, zx_gpaddr_t addr,
                                 uint16_t size)
    : phys_mem_(phys_mem),
      desc_(addr),
      avail_(desc_ + desc_size(size)),
      used_(avail_ + avail_size(size)),
      end_(used_ + used_size(size)) {
  // Configure the ring size.
  ring_.size = size;
}

void VirtioQueueFake::Configure(zx_gpaddr_t data_addr, size_t data_len) {
  // Configure the descriptor table.
  ring_.desc = phys_mem_.as<vring_desc>(desc_, avail_ - desc_);

  // Configure the available ring.
  ring_.avail =
      phys_mem_.as<vring_avail>(avail_, used_ - sizeof(uint16_t) - avail_);
  ring_.used_event = phys_mem_.as<uint16_t>(used_ - sizeof(uint16_t));

  // Configure the used ring.
  ring_.used = phys_mem_.as<vring_used>(used_, end_ - sizeof(uint16_t) - used_);
  ring_.avail_event = phys_mem_.as<uint16_t>(end_ - sizeof(uint16_t));

  // Configure data addresses.
  data_begin_ = data_addr;
  data_end_ = data_addr + data_len;
}

zx_status_t VirtioQueueFake::WriteDesc(void** buf, uint32_t len, uint16_t flags,
                                       uint16_t* desc_idx) {
  *desc_idx = next_desc_++ % ring_.size;
  if (data_begin_ + len >= data_end_) {
    return ZX_ERR_NO_MEMORY;
  }

  void* data = phys_mem_.as<void>(data_begin_, len);
  if (flags & VRING_DESC_F_WRITE) {
    *buf = data;
  } else {
    memcpy(data, *buf, len);
  }

  auto& desc = const_cast<volatile vring_desc&>(ring_.desc[*desc_idx]);
  desc.addr = data_begin_;
  desc.len = len;
  desc.flags = flags;

  data_begin_ += len;
  return ZX_OK;
}

void VirtioQueueFake::WriteAvail(uint16_t head_idx) {
  auto& avail = const_cast<volatile vring_avail&>(*ring_.avail);
  auto& idx = const_cast<uint16_t&>(ring_.avail->idx);
  avail.ring[idx++ % ring_.size] = head_idx;
}

zx_status_t VirtioQueueFake::SetNext(uint16_t desc_idx, uint16_t next_idx) {
  if (desc_idx >= ring_.size || next_idx >= ring_.size) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  auto& desc = const_cast<volatile vring_desc&>(ring_.desc[desc_idx]);
  desc.flags |= VRING_DESC_F_NEXT;
  desc.next = next_idx;
  return ZX_OK;
}

std::optional<VirtioQueueFake::UsedElement> VirtioQueueFake::NextUsed() {
  if (ring_.used->idx != used_index_) {
    const auto& elem = ring_.used->ring[used_index_++ % ring_.size];
    return VirtioQueueFake::UsedElement{elem.id, elem.len};
  } else {
    return std::nullopt;
  }
}

DescriptorChainBuilder::DescriptorChainBuilder(VirtioQueueFake& queue_fake)
    : queue_fake_(queue_fake) {}

DescriptorChainBuilder& DescriptorChainBuilder::AppendDescriptor(
    void** buf, uint32_t len, uint16_t flags) {
  if (status_ != ZX_OK) {
    return *this;
  }

  uint16_t desc_idx;
  status_ = queue_fake_.WriteDesc(buf, len, flags, &desc_idx);
  if (status_ != ZX_OK) {
    return *this;
  } else if (chain_len_++ == 0) {
    head_idx_ = desc_idx;
  } else {
    status_ = queue_fake_.SetNext(prev_idx_, desc_idx);
  }
  prev_idx_ = desc_idx;
  return *this;
}

DescriptorChainBuilder& DescriptorChainBuilder::AppendReadableDescriptor(
    const void* buf, uint32_t len) {
  return AppendDescriptor(const_cast<void**>(&buf), len, 0);
}

DescriptorChainBuilder& DescriptorChainBuilder::AppendWritableDescriptor(
    void** buf, uint32_t len) {
  return AppendDescriptor(buf, len, VRING_DESC_F_WRITE);
}

zx_status_t DescriptorChainBuilder::Build(uint16_t* out_index) {
  if (status_ != ZX_OK) {
    return status_;
  }
  queue_fake_.WriteAvail(head_idx_);
  if (out_index) {
    *out_index = head_idx_;
  }
  status_ = ZX_ERR_BAD_STATE;
  return ZX_OK;
}
