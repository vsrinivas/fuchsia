// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/virtio_queue_waiter.h"

#include "lib/fxl/logging.h"

namespace machina {

VirtioQueueWaiter::VirtioQueueWaiter(async_dispatcher_t* dispatcher,
                                     VirtioQueue* queue, Handler handler)
    : wait_(this, queue->event(), VirtioQueue::SIGNAL_QUEUE_AVAIL),
      dispatcher_(dispatcher),
      queue_(queue),
      handler_(std::move(handler)) {}

VirtioQueueWaiter::~VirtioQueueWaiter() { Cancel(); }

zx_status_t VirtioQueueWaiter::Begin() {
  zx_status_t status = ZX_OK;

  fbl::AutoLock lock(&mutex_);
  if (!pending_) {
    status = wait_.Begin(dispatcher_);
    if (status == ZX_OK) {
      pending_ = true;
    }
  }
  return status;
}

void VirtioQueueWaiter::Cancel() {
  fbl::AutoLock lock(&mutex_);
  if (pending_) {
    wait_.Cancel();
    pending_ = false;
  }
}

void VirtioQueueWaiter::WaitHandler(async_dispatcher_t* dispatcher,
                                    async::WaitBase* wait, zx_status_t status,
                                    const zx_packet_signal_t* signal) {
  uint16_t index = 0;
  {
    fbl::AutoLock lock(&mutex_);
    if (!pending_) {
      return;
    }

    // Only invoke the handler if we can get a descriptor.
    if (status == ZX_OK) {
      status = queue_->NextAvail(&index);
      if (status == ZX_ERR_SHOULD_WAIT) {
        status = wait->Begin(dispatcher);
        if (status == ZX_OK) {
          return;
        }
      }
    }
    pending_ = false;
  }
  handler_(status, index);
}

}  // namespace machina
