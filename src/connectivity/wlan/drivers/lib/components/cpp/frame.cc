// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <wlan/drivers/components/frame.h>
#include <wlan/drivers/components/frame_storage.h>

namespace wlan::drivers::components {

void Frame::ReturnToStorage() {
  if (storage_) {
    Restore();
    std::lock_guard lock(*storage_);
    storage_->Store(std::move(*this));
  }
}

// This class is used in performance sensitive areas and will be copied a lot. Be careful with
// expanding its size as it will lead to increased time spent copying and less efficient cache
// usage.
static_assert(sizeof(Frame) <= 32, "Be mindful when expanding this class");

}  // namespace wlan::drivers::components
