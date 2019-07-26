// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/auto_lock.h>

#include <io-scheduler/queue.h>
#include <io-scheduler/io-scheduler.h>

namespace ioscheduler {

Queue::~Queue() { ZX_DEBUG_ASSERT(active_list_.is_empty()); }

zx_status_t Queue::GetNextStream(bool wait, StreamRef* stream_out) {
  fbl::AutoLock lock(&lock_);
  while (active_list_.is_empty()) {
    if (sched_->ShutdownInitiated()) {
      return ZX_ERR_CANCELED;
    }
    if (!wait) {
      return ZX_ERR_SHOULD_WAIT;
    }
    active_available_.Wait(&lock_);
  }
  ZX_DEBUG_ASSERT(!active_list_.is_empty());
  *stream_out = active_list_.pop_front();
  return ZX_OK;
}

void Queue::SetActive(StreamRef stream) {
  fbl::AutoLock lock(&lock_);
  bool was_empty = active_list_.is_empty();
  active_list_.push_back(std::move(stream));
  if (was_empty) {
    active_available_.Broadcast();
  }
}

void Queue::SignalAvailable() {
  fbl::AutoLock lock(&lock_);
  active_available_.Broadcast();
}

}  // namespace ioscheduler
