// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_VIRTIO_QUEUE_WAITER_H_
#define GARNET_LIB_MACHINA_VIRTIO_QUEUE_WAITER_H_

#include <fbl/function.h>
#include <lib/async/cpp/wait.h>
#include <zircon/types.h>

#include "garnet/lib/machina/virtio_queue.h"

namespace machina {

// Helper for performing async waits against a virtio queue.
class VirtioQueueWaiter {
 public:
  using Handler = fbl::Function<void(zx_status_t, uint16_t index)>;
  VirtioQueueWaiter(async_t* async, VirtioQueue* queue, Handler handler);
  ~VirtioQueueWaiter();

  zx_status_t Begin();
  void Cancel();

 private:
  async_wait_result_t WaitHandler(async_t* async,
                                  zx_status_t status,
                                  const zx_packet_signal_t* signal);

  fbl::Mutex mutex_;
  async::Wait wait_ __TA_GUARDED(mutex_);
  async_t* const async_;
  VirtioQueue* const queue_ __TA_GUARDED(mutex_);
  const Handler handler_;
  bool pending_ __TA_GUARDED(mutex_) = false;
};

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_VIRTIO_QUEUE_WAITER_H_
