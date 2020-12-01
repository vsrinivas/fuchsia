// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_PIN_EXECUTABLE_MEMORY_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_PIN_EXECUTABLE_MEMORY_H_

#include <lib/async-loop/cpp/loop.h>
#include <zircon/syscalls/object.h>

#include <mutex>
#include <vector>

#include "src/lib/fxl/synchronization/thread_annotations.h"

namespace media::audio {

// Spins up a background thread to periodically touch all pages of executable
// memory, which keeps our executable pages on the "recently used" list and prevents
// them from being paged out. This is a hacky implementation of memory pinning.
// We are using this temporarily until Zircon provides a better solution.
// See fxbug.dev/62830.
class PinExecutableMemory {
 public:
  // Return the singleton object. Executable memory is pinned the first time this
  // function is called and periodically thereafter. If on-demand pinning is desired,
  // use Singleton().Pin().
  static PinExecutableMemory& Singleton();

  // Pins all executable memory. Thread-safe.
  void Pin();

 private:
  PinExecutableMemory();
  void PeriodicPin();
  std::vector<zx_info_maps_t> ListVMaps();

  async::Loop loop_;

  std::mutex mutex_;
  size_t last_pinned_bytes_ FXL_GUARDED_BY(mutex_) = 0;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_PIN_EXECUTABLE_MEMORY_H_
