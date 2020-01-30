// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/netbuf.h"

#include <functional>
#include <utility>

// Additional includes for brcmf_netbuf.

#include <stdlib.h>
#include <sys/types.h>

#include "debug.h"

namespace wlan {
namespace brcmfmac {

Netbuf::Netbuf() = default;

Netbuf::~Netbuf() {
  // Make sure the Return() function has already been called, and that it clears out data_ and
  // size_.
  ZX_DEBUG_ASSERT(data_ == nullptr);
  ZX_DEBUG_ASSERT(size_ == 0);
};

const void* Netbuf::data() const { return data_; };

size_t Netbuf::size() const { return size_; }

void Netbuf::Return(zx_status_t status) {
  data_ = nullptr;
  size_ = 0;
}

EthernetNetbuf::EthernetNetbuf() = default;

EthernetNetbuf::EthernetNetbuf(ethernet_netbuf* netbuf,
                               ethernet_impl_queue_tx_callback completion_cb, void* cookie)
    : netbuf_(netbuf), completion_cb_(completion_cb), cookie_(cookie) {
  data_ = netbuf_->data_buffer;
  size_ = netbuf_->data_size;
}

EthernetNetbuf::EthernetNetbuf(EthernetNetbuf&& other) { swap(*this, other); }

EthernetNetbuf& EthernetNetbuf::operator=(EthernetNetbuf other) {
  swap(*this, other);
  return *this;
}

void swap(EthernetNetbuf& lhs, EthernetNetbuf& rhs) {
  using std::swap;
  swap(static_cast<Netbuf&>(lhs), static_cast<Netbuf&>(rhs));
  swap(lhs.netbuf_, rhs.netbuf_);
  swap(lhs.completion_cb_, rhs.completion_cb_);
  swap(lhs.cookie_, rhs.cookie_);
}

EthernetNetbuf::~EthernetNetbuf() {
  if (completion_cb_ != nullptr) {
    Return(ZX_ERR_INTERNAL);
  }
}

void EthernetNetbuf::Return(zx_status_t status) {
  Netbuf::Return(status);

  if (completion_cb_ == nullptr) {
    return;
  }
  std::invoke(completion_cb_, cookie_, status, netbuf_);
  netbuf_ = nullptr;
  completion_cb_ = nullptr;
  cookie_ = nullptr;
}

AllocatedNetbuf::AllocatedNetbuf() = default;

AllocatedNetbuf::AllocatedNetbuf(std::unique_ptr<char[]> allocation, size_t size)
    : allocation_(std::move(allocation)) {
  data_ = static_cast<const void*>(allocation_.get());
  size_ = size;
}

AllocatedNetbuf::AllocatedNetbuf(AllocatedNetbuf&& other) { swap(*this, other); }

AllocatedNetbuf& AllocatedNetbuf::operator=(AllocatedNetbuf other) {
  swap(*this, other);
  return *this;
}

void swap(AllocatedNetbuf& lhs, AllocatedNetbuf& rhs) {
  using std::swap;
  swap(static_cast<Netbuf&>(lhs), static_cast<Netbuf&>(rhs));
  swap(lhs.allocation_, rhs.allocation_);
}

AllocatedNetbuf::~AllocatedNetbuf() {
  if (allocation_ != nullptr) {
    Return(ZX_ERR_INTERNAL);
  }
}

void AllocatedNetbuf::Return(zx_status_t status) {
  Netbuf::Return(status);
  allocation_.reset();
}

}  // namespace brcmfmac
}  // namespace wlan

//
// Transitional brcmf_netbuf definitions below.
//

struct brcmf_netbuf* brcmf_netbuf_allocate(uint32_t size) {
  struct brcmf_netbuf* netbuf = static_cast<decltype(netbuf)>(calloc(1, sizeof(*netbuf)));
  if (netbuf == NULL) {
    return NULL;
  }
  // Align the allocation size to a multiple of 4.  SDIO transactions require 4-byte alignment,
  // so to avoid having to reallocate and copy data for odd-size transactions we make sure the
  // underlying buffer is already aligned and directly usable.
  uint32_t aligned_size = (size + 3) & ~3;
  netbuf->data = netbuf->allocated_buffer =
      static_cast<decltype(netbuf->allocated_buffer)>(malloc(aligned_size));
  if (netbuf->data == NULL) {
    free(netbuf);
    return NULL;
  }
  netbuf->allocated_size = aligned_size;
  return netbuf;
}

void brcmf_netbuf_free(struct brcmf_netbuf* netbuf) {
  free(netbuf->allocated_buffer);
  free(netbuf);
}
