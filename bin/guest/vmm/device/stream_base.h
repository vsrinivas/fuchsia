// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_GUEST_VMM_DEVICE_STREAM_BASE_H_
#define GARNET_BIN_GUEST_VMM_DEVICE_STREAM_BASE_H_

#include "garnet/lib/machina/device/virtio_queue.h"

// Abstracts out the queue handling logic into a stream.
class StreamBase {
 public:
  void Init(const machina::PhysMem& phys_mem,
            machina::VirtioQueue::InterruptFn interrupt) {
    queue_.set_phys_mem(&phys_mem);
    queue_.set_interrupt(std::move(interrupt));
  }

  void Configure(uint16_t size, zx_gpaddr_t desc, zx_gpaddr_t avail,
                 zx_gpaddr_t used) {
    queue_.Configure(size, desc, avail, used);
  }

  uint32_t* Used() { return chain_.Used(); }

 protected:
  machina::VirtioQueue queue_;
  machina::VirtioChain chain_;
  machina::VirtioDescriptor desc_;
};

#endif  // GARNET_BIN_GUEST_VMM_DEVICE_STREAM_BASE_H_
