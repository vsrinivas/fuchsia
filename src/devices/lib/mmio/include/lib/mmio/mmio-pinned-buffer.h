// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_MMIO_INCLUDE_LIB_MMIO_MMIO_PINNED_BUFFER_H_
#define SRC_DEVICES_LIB_MMIO_INCLUDE_LIB_MMIO_MMIO_PINNED_BUFFER_H_

#include <lib/mmio/mmio-internal.h>
#include <zircon/assert.h>
#include <zircon/process.h>

__BEGIN_CDECLS

typedef struct {
  const mmio_buffer_t* mmio;
  zx_handle_t pmt;
  // |paddr| points to the content starting at |mmio->offset| in |mmio->vmo|.
  zx_paddr_t paddr;
} mmio_pinned_buffer_t;

// Returns a pinned buffer if successful. |buffer| must outlive |out|.
//
// Example usage: A device needs access to another device's MMIO space.
zx_status_t mmio_buffer_pin(mmio_buffer_t* buffer, zx_handle_t bti, mmio_pinned_buffer_t* out);

// Unpins the buffer.
void mmio_buffer_unpin(mmio_pinned_buffer_t* buffer);

__END_CDECLS

#ifdef __cplusplus

#include <algorithm>

namespace fdf {

// MmioPinnedBuffer is wrapper around mmio_pinned_buffer_t.
class MmioPinnedBuffer {
 public:
  // DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE
  MmioPinnedBuffer(const MmioPinnedBuffer&) = delete;
  MmioPinnedBuffer& operator=(const MmioPinnedBuffer&) = delete;

  explicit MmioPinnedBuffer(mmio_pinned_buffer_t pinned) : pinned_(pinned) {
    ZX_ASSERT(pinned_.paddr != 0);
  }

  ~MmioPinnedBuffer() { mmio_buffer_unpin(&pinned_); }

  MmioPinnedBuffer(MmioPinnedBuffer&& other) { transfer(std::move(other)); }

  MmioPinnedBuffer& operator=(MmioPinnedBuffer&& other) {
    transfer(std::move(other));
    return *this;
  }

  void reset() {
    mmio_buffer_unpin(&pinned_);
    memset(&pinned_, 0, sizeof(pinned_));
  }

  zx_paddr_t get_paddr() const { return pinned_.paddr; }

 private:
  void transfer(MmioPinnedBuffer&& other) {
    pinned_ = other.pinned_;
    memset(&other.pinned_, 0, sizeof(other.pinned_));
  }
  mmio_pinned_buffer_t pinned_;
};

}  // namespace fdf

#endif

#endif  // SRC_DEVICES_LIB_MMIO_INCLUDE_LIB_MMIO_MMIO_PINNED_BUFFER_H_
