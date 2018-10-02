// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_DEVICE_STREAM_BASE_H_
#define GARNET_LIB_MACHINA_DEVICE_STREAM_BASE_H_

#include "garnet/lib/machina/device/virtio_queue.h"

namespace machina {

// Abstracts out the queue handling logic into a stream.
struct StreamBase {
  machina::VirtioQueue queue;
  machina::VirtioDescriptor desc;
  uint16_t head;
  uint32_t used;

  StreamBase() { Reset(); }

  void Init(const machina::PhysMem& phys_mem,
            machina::VirtioQueue::InterruptFn interrupt) {
    queue.set_phys_mem(&phys_mem);
    queue.set_interrupt(std::move(interrupt));
  }

  // Fetches the next descriptor in a chain, otherwise returns false.
  bool NextDescriptor() {
    if (!desc.has_next) {
      return false;
    }
    if (desc.addr == nullptr) {
      zx_status_t status = queue.NextAvail(&head);
      FXL_CHECK(status == ZX_OK)
          << "Failed to find an available descriptor " << status;
      desc.next = head;
    }
    zx_status_t status = queue.ReadDesc(desc.next, &desc);
    FXL_CHECK(status == ZX_OK) << "Failed to read descriptor " << status;
    return true;
  }

  // Returns the descriptor chain back to the queue.
  void ReturnChain() {
    zx_status_t status = queue.Return(head, used);
    FXL_CHECK(status == ZX_OK)
        << "Failed to return descriptor to queue " << status;
    Reset();
  }

  // Returns whether we have a descriptor chain available to process.
  bool HasChain() { return queue.HasAvail(); }
  // Returns whether we are in the middle of processing a descriptor.
  bool HasDescriptor() { return desc.addr != nullptr; }

 private:
  void Reset() {
    desc.addr = nullptr;
    desc.has_next = true;
    used = 0;
  }
};

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_DEVICE_STREAM_BASE_H_
