// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/virtio_queue_waiter.h"

#include "lib/fxl/logging.h"

namespace machina {

VirtioQueueWaiter::VirtioQueueWaiter(async_t* async,
                                     VirtioQueue* queue,
                                     Handler handler)
    : wait_(queue->event(), VirtioQueue::SIGNAL_QUEUE_AVAIL),
      async_(async),
      queue_(queue),
      handler_(fbl::move(handler)) {
  wait_.set_handler(fbl::BindMember(this, &VirtioQueueWaiter::WaitHandler));
}

VirtioQueueWaiter::~VirtioQueueWaiter() {
  Cancel();
}

zx_status_t VirtioQueueWaiter::Begin() {
  zx_status_t status = ZX_OK;

  fbl::AutoLock lock(&mutex_);
  if (!pending_) {
    status = wait_.Begin(async_);
    if (status == ZX_OK) {
      pending_ = true;
    }
  }
  return status;
}

void VirtioQueueWaiter::Cancel() {
  fbl::AutoLock lock(&mutex_);
  if (pending_) {
    wait_.Cancel(async_);
    pending_ = false;
  }
}

async_wait_result_t VirtioQueueWaiter::WaitHandler(
    async_t* async,
    zx_status_t status,
    const zx_packet_signal_t* signal) {
  uint16_t index = 0;
  {
    fbl::AutoLock lock(&mutex_);
    if (!pending_) {
      return ASYNC_WAIT_FINISHED;
    }

    // Only invoke the handler if we can get a descriptor.
    if (status == ZX_OK) {
      status = queue_->NextAvail(&index);
      if (status == ZX_ERR_SHOULD_WAIT) {
        return ASYNC_WAIT_AGAIN;
      }
    }
    pending_ = false;
  }
  handler_(status, index);
  return ASYNC_WAIT_FINISHED;
}

}  // namespace machina
