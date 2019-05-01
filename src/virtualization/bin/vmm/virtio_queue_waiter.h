// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_VIRTIO_QUEUE_WAITER_H_
#define SRC_VIRTUALIZATION_BIN_VMM_VIRTIO_QUEUE_WAITER_H_

#include <lib/async/cpp/wait.h>
#include <lib/fit/function.h>
#include <zircon/types.h>

#include "src/virtualization/bin/vmm/device/virtio_queue.h"

// Helper for performing async waits against a virtio queue.
class VirtioQueueWaiter {
 public:
  using Handler = fit::function<void(zx_status_t, uint16_t index)>;
  VirtioQueueWaiter(async_dispatcher_t* dispatcher, VirtioQueue* queue,
                    Handler handler);
  ~VirtioQueueWaiter();

  zx_status_t Begin();
  void Cancel();

 private:
  void WaitHandler(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                   zx_status_t status, const zx_packet_signal_t* signal);

  std::mutex mutex_;
  async::WaitMethod<VirtioQueueWaiter, &VirtioQueueWaiter::WaitHandler> wait_
      __TA_GUARDED(mutex_){this};
  async_dispatcher_t* const dispatcher_;
  VirtioQueue* const queue_ __TA_GUARDED(mutex_);
  const Handler handler_;
  bool pending_ __TA_GUARDED(mutex_) = false;
};

#endif  // SRC_VIRTUALIZATION_BIN_VMM_VIRTIO_QUEUE_WAITER_H_
