// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_V1_PIN_EXECUTABLE_MEMORY_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_V1_PIN_EXECUTABLE_MEMORY_H_

#include <fuchsia/mem/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <zircon/syscalls/object.h>

#include <mutex>
#include <unordered_map>
#include <vector>

namespace media::audio {

// Spins up a background thread to periodically ensure that all executable memory
// is pinned. See fxbug.dev/62830 for the original motivation.
class PinExecutableMemory {
 public:
  // Return the singleton object. Executable memory is pinned the first time this
  // function is called and periodically thereafter. If on-demand pinning is desired,
  // use Singleton().Pin().
  static PinExecutableMemory& Singleton();

  void Pin();

 private:
  PinExecutableMemory();
  PinExecutableMemory(PinExecutableMemory&) = delete;
  PinExecutableMemory(PinExecutableMemory&&) = delete;

  void PeriodicPin();
  std::vector<zx_info_maps_t> ListVMaps();

  struct Mapping {
    uint64_t size;
    zx_koid_t vmo_koid;
    uint64_t vmo_offset;
  };

  async::Loop loop_;
  size_t pinned_bytes_ = 0;

  // Old mappings are not always removed. For example, if we map a VMO at address
  // X, then later unmap that VMO and map a different VMO at address X-1, we'll end
  // up with two entries in this table: one each for the old and new mappings.
  // Although this is unbounded growth in theory, in practice mappings are rarely
  // removed so there's no danger of OOM. We could bound growth with something like
  // a k-d tree, but that adds unnecessary complexity.
  std::unordered_map<uint64_t, Mapping> pinned_mappings_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_V1_PIN_EXECUTABLE_MEMORY_H_
