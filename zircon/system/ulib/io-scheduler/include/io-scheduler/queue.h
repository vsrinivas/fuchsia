// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/condition_variable.h>
#include <fbl/mutex.h>
#include <zircon/types.h>

#include <io-scheduler/stream.h>

namespace ioscheduler {

class Queue {
 public:
  Queue(Scheduler* sched) : sched_(sched) {}
  ~Queue();

  // Get the next stream containing ops to be issued.
  // Returned stream must be requeued via SetActive() if it contains valid ops.
  zx_status_t GetNextStream(bool wait, StreamRef* stream_out);

  // Set a stream as having ops ready to be issued.
  void SetActive(StreamRef stream) __TA_EXCLUDES(lock_);

  // Signal to waiters that new streams are available in queue.
  void SignalAvailable() __TA_EXCLUDES(lock_);

 private:
  using StreamRefList = Stream::ListUnsorted;

  // Pointer to parent scheduler. This pointer does not imply ownership.
  Scheduler* sched_ = nullptr;

  fbl::Mutex lock_;
  // List of streams that have ops ready to be scheduled.
  // In the future this will be a more complex, priority-ordered data structure.
  StreamRefList active_list_ __TA_GUARDED(lock_);
  // Event notifying waiters that active streams are available.
  fbl::ConditionVariable active_available_ __TA_GUARDED(lock_);
};

}  // namespace ioscheduler
