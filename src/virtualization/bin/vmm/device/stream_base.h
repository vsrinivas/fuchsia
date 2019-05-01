// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_DEVICE_STREAM_BASE_H_
#define SRC_VIRTUALIZATION_BIN_VMM_DEVICE_STREAM_BASE_H_

#include "src/virtualization/bin/vmm/device/virtio_queue.h"

// Abstracts out the queue handling logic into a stream.
class StreamBase {
 public:
  void Init(const PhysMem& phys_mem, VirtioQueue::InterruptFn interrupt) {
    queue_.set_phys_mem(&phys_mem);
    queue_.set_interrupt(std::move(interrupt));
  }

  void Configure(uint16_t size, zx_gpaddr_t desc, zx_gpaddr_t avail,
                 zx_gpaddr_t used) {
    queue_.Configure(size, desc, avail, used);
  }

  uint32_t* Used() { return chain_.Used(); }

 protected:
  VirtioQueue queue_;
  VirtioChain chain_;
  VirtioDescriptor desc_;
};

#endif  // SRC_VIRTUALIZATION_BIN_VMM_DEVICE_STREAM_BASE_H_
