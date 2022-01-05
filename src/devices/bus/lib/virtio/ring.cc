// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <inttypes.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/driver.h>
#include <lib/stdcompat/bit.h>
#include <lib/virtio/device.h>
#include <lib/virtio/ring.h>
#include <lib/zx/vmar.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>

namespace virtio {

void virtio_dump_desc(const struct vring_desc* desc) {
  printf("vring descriptor %p: ", desc);
  printf("[addr=%#" PRIx64 ", ", desc->addr);
  printf("len=%d, ", desc->len);
  printf("flags=%#04hx, ", desc->flags);
  printf("next=%#04hx]\n", desc->next);
}

Ring::Ring(Device* device) : device_(device) { memset(&ring_buf_, 0, sizeof(ring_buf_)); }

Ring::Ring(Ring&& other) noexcept {
  index_ = other.index_;
  device_ = other.device_;
  other.device_ = nullptr;
  ring_buf_ = other.ring_buf_;
  other.ring_buf_ = io_buffer_t{};
  ring_ = other.ring_;
  other.ring_ = vring{};
}

Ring::~Ring() { io_buffer_release(&ring_buf_); }

Ring& Ring::operator=(Ring&& other) noexcept {
  io_buffer_release(&ring_buf_);
  index_ = other.index_;
  device_ = other.device_;
  other.device_ = nullptr;
  ring_buf_ = other.ring_buf_;
  other.ring_buf_ = io_buffer_t{};
  ring_ = other.ring_;
  other.ring_ = vring{};
  return *this;
}

zx_status_t Ring::Init(uint16_t index) {
  return Init(/*index=*/index, /*count=*/device_->GetRingSize(index));
}

zx_status_t Ring::Init(uint16_t index, uint16_t count) {
  zxlogf(TRACE, "%s: index %u, count %u", __func__, index, count);

  // check that count is a power of 2
  if (!cpp20::has_single_bit(count)) {
    zxlogf(ERROR, "ring count: %u is not a power of 2", count);
    return ZX_ERR_INVALID_ARGS;
  }

  index_ = index;

  // make sure the count is available in this ring
  uint16_t max_ring_size = device_->GetRingSize(index);
  if (count > max_ring_size) {
    zxlogf(ERROR, "ring init count too big for hardware %u > %u", count, max_ring_size);
    return ZX_ERR_OUT_OF_RANGE;
  }

  // allocate a ring
  size_t size = vring_size(count, zx_system_get_page_size());
  zxlogf(TRACE, "%s: need %zu bytes", __func__, size);

  zx_status_t status =
      io_buffer_init(&ring_buf_, device_->bti().get(), size, IO_BUFFER_RW | IO_BUFFER_CONTIG);
  if (status != ZX_OK) {
    return status;
  }

  zxlogf(TRACE, "%s: allocated vring at %p, physical address %#" PRIxPTR, __func__,
         io_buffer_virt(&ring_buf_), io_buffer_phys(&ring_buf_));

  /* initialize the ring */
  vring_init(&ring_, count, io_buffer_virt(&ring_buf_), zx_system_get_page_size());
  ring_.free_list = 0xffff;
  ring_.free_count = 0;

  /* add all the descriptors to the free list */
  for (uint16_t i = 0; i < count; i++) {
    FreeDesc(i);
  }

  /* register the ring with the device */
  zx_paddr_t pa_desc = io_buffer_phys(&ring_buf_);
  zx_paddr_t pa_avail = pa_desc + ((uintptr_t)ring_.avail - (uintptr_t)ring_.desc);
  zx_paddr_t pa_used = pa_desc + ((uintptr_t)ring_.used - (uintptr_t)ring_.desc);
  device_->SetRing(index_, count, pa_desc, pa_avail, pa_used);

  return ZX_OK;
}

void Ring::FreeDesc(uint16_t desc_index) {
  zxlogf(TRACE, "%s: index %u free_count %u", __func__, desc_index, ring_.free_count);
  ring_.desc[desc_index].next = ring_.free_list;
  ring_.free_list = desc_index;
  ring_.free_count++;
}

struct vring_desc* Ring::AllocDescChain(uint16_t count, uint16_t* start_index) {
  if (ring_.free_count < count)
    return nullptr;

  /* start popping entries off the chain */
  struct vring_desc* last = 0;
  uint16_t last_index = 0;
  while (count > 0) {
    uint16_t i = ring_.free_list;
    assert(i < ring_.num);

    struct vring_desc* desc = &ring_.desc[i];

    ring_.free_list = desc->next;
    ring_.free_count--;

    if (last) {
      desc->flags |= VRING_DESC_F_NEXT;
      desc->next = last_index;
    } else {
      // first one
      desc->flags &= static_cast<uint16_t>(~VRING_DESC_F_NEXT);
      desc->next = 0;
    }
    last = desc;
    last_index = i;
    count--;
  }

  if (start_index)
    *start_index = last_index;

  return last;
}

void Ring::SubmitChain(uint16_t desc_index) {
  zxlogf(TRACE, "%s: desc %u", __func__, desc_index);

  /* add the chain to the available list */
  struct vring_avail* avail = ring_.avail;

  avail->ring[avail->idx & ring_.num_mask] = desc_index;
  // Write memory barrier before updating avail->idx; updates to the descriptor ring must be
  // visible before an updated avail->idx.
  hw_wmb();
  avail->idx++;
}

void Ring::Kick() {
  zxlogf(TRACE, "%s: entry", __func__);
  // Write memory barrier before notifying the device. Updates to avail->idx must be visible
  // before the device sees the wakeup notification (so it processes the latest descriptors).
  hw_mb();

  device_->RingKick(index_);
}

}  // namespace virtio
