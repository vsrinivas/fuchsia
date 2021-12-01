// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <wlan/drivers/components/frame_container.h>
#include <wlan/drivers/components/frame_storage.h>

namespace wlan::drivers::components {

void FrameContainer::ReturnToStorage() {
  if (container_.empty()) {
    return;
  }

  // Ignore all frames at the start of the container that have no storage, do not return these.
  auto frame = container_.begin();
  while (frame != container_.end() && frame->storage_ == nullptr) {
    ++frame;
  }
  if (frame == container_.end()) {
    return;
  }

  // The next frame is guaranteed to have storage, so we can safely lock it. The frames following
  // the first frame may or may not have storage and they may have different storage. Keep track
  // of the current storage and make sure we handle locks correctly.
  FrameStorage* storage = frame->storage_;
  storage->lock();
  for (; frame != container_.end(); ++frame) {
    if (!frame->storage_) {
      // Don't return frames without storage
      continue;
    }
    if (frame->storage_ != storage) {
      // This frame has a different storage than the previous frame, unlock the previous storage
      // then track the new storage and lock it. At this point we're guaranteed that both storage
      // and frame->storage_ are not null (because of the earlier check and we only assign to
      // storage when we know it's not going to be null).
      storage->unlock();
      storage = frame->storage_;
      storage->lock();
    }
    frame->Restore();
    storage->Store(std::move(*frame));
  }
  // Since we make sure to never assign null to storage we can (and should) safely unlock it here.
  storage->unlock();
}

}  // namespace wlan::drivers::components
