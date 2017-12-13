// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/input_dispatcher.h"

#include <fbl/auto_lock.h>

namespace machina {

InputDispatcher::InputDispatcher(size_t queue_depth)
    : pending_(new InputEvent[queue_depth], queue_depth) {
  cnd_init(&signal_);
}

size_t InputDispatcher::size() const {
  fbl::AutoLock lock(&mutex_);
  return size_;
}

void InputDispatcher::PostEvent(const InputEvent event) {
  fbl::AutoLock lock(&mutex_);
  pending_[(index_ + size_) % pending_.size()] = event;
  if (size_ < pending_.size()) {
    size_++;
  } else {
    // Ring is full.
    DropOldestLocked();
  }

  cnd_signal(&signal_);
}

InputEvent InputDispatcher::Wait() {
  fbl::AutoLock lock(&mutex_);
  while (size_ == 0) {
    cnd_wait(&signal_, mutex_.GetInternal());
  }
  InputEvent result = pending_[index_];
  DropOldestLocked();
  size_--;
  return result;
}

void InputDispatcher::DropOldestLocked() {
  index_ = (index_ + 1) % pending_.size();
}

}  // namespace machina
