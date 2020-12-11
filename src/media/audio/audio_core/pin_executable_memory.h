// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_PIN_EXECUTABLE_MEMORY_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_PIN_EXECUTABLE_MEMORY_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/fzl/vmo-mapper.h>
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

  // Our Pin() implementation assumes that mappings are not concurrently discarded.
  // To prevent races, dynamic mappings must be made through the following object, which
  // synchronizes with Pin() when the mapping is destructed.
  class VmoMapper {
   public:
    VmoMapper() = default;
    VmoMapper(VmoMapper&&) = default;
    VmoMapper(VmoMapper&) = delete;

    ~VmoMapper() {
      if (!mapper_.start()) {
        return;
      }
      auto& pinner = PinExecutableMemory::Singleton();
      std::lock_guard<std::mutex> lock(pinner.mutex_);
      pinner.discarded_mappings_.push_back({
          .start = reinterpret_cast<size_t>(start()),
          .end = reinterpret_cast<size_t>(start()) + size(),
      });
      mapper_.Unmap();
    }

    zx_status_t CreateAndMap(uint64_t size, zx_vm_option_t map_flags,
                             fbl::RefPtr<fzl::VmarManager> vmar_manager = nullptr,
                             zx::vmo* vmo_out = nullptr,
                             zx_rights_t vmo_rights = ZX_RIGHT_SAME_RIGHTS,
                             uint32_t cache_policy = 0, uint32_t vmo_options = 0) {
      return mapper_.CreateAndMap(size, map_flags, vmar_manager, vmo_out, vmo_rights, cache_policy,
                                  vmo_options);
    }

    zx_status_t Map(const zx::vmo& vmo, uint64_t offset, uint64_t size, zx_vm_option_t map_flags,
                    fbl::RefPtr<fzl::VmarManager> vmar_manager = nullptr) {
      return mapper_.Map(vmo, offset, size, map_flags, vmar_manager);
    }

    void* start() const { return mapper_.start(); }
    uint64_t size() const { return mapper_.size(); }

   private:
    fzl::VmoMapper mapper_;
  };

 private:
  friend class VmoMapper;

  PinExecutableMemory();
  PinExecutableMemory(PinExecutableMemory&) = delete;
  PinExecutableMemory(PinExecutableMemory&&) = delete;

  void PeriodicPin();
  std::vector<zx_info_maps_t> ListVMaps();
  bool ShouldSkip(size_t addr) const FXL_REQUIRE(mutex_);

  // Thread annotation workaround for non-scoped lock acquisitions.
  void AssertMutexHeld() __attribute__((assert_capability(mutex_))) {}

  struct Range {
    size_t start;
    size_t end;
  };

  async::Loop loop_;

  std::mutex mutex_;
  std::vector<Range> discarded_mappings_ FXL_GUARDED_BY(mutex_);
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_PIN_EXECUTABLE_MEMORY_H_
