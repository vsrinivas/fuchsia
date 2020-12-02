// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/pin_executable_memory.h"

#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>
#include <lib/zx/clock.h>
#include <lib/zx/process.h>
#include <lib/zx/thread.h>
#include <zircon/status.h>

#include <iomanip>

namespace media::audio {

// Note: This is available in as PAGE_SIZE from <arch/defines.h>, but that header
// is not available outside of the kernel. We expect PAGE_SIZE to be no smaller than
// this value (it may be larger, e.g. 16k or 64k on some architectures).
static constexpr auto kPageSizeBytes = 4096;

// Memory is considered "unused" if it has not been touched for more than 30s.
// To keep all executable memory pinned, we must run at least once every 30s.
// To ensure we never miss a deadline, do this twice every 30s.
static constexpr auto kTimeBetweenPins = zx::sec(15);

PinExecutableMemory& PinExecutableMemory::Singleton() {
  static PinExecutableMemory* p = new PinExecutableMemory;
  return *p;
}

PinExecutableMemory::PinExecutableMemory() : loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {
  loop_.StartThread("pin-executable-memory");
  PeriodicPin();
}

void PinExecutableMemory::PeriodicPin() {
  const auto next_task_time = zx::clock::get_monotonic() + kTimeBetweenPins;
  Pin();
  async::PostTaskForTime(
      loop_.dispatcher(), [this]() { PeriodicPin(); }, next_task_time);
}

void PinExecutableMemory::Pin() {
  TRACE_DURATION("audio", "PinExecutableMemory::Pin");

  auto start_time = zx::clock::get_monotonic();
  size_t total_bytes = 0;

  for (auto& vmap : ListVMaps()) {
    if (vmap.type != ZX_INFO_MAPS_TYPE_MAPPING) {
      continue;
    }
    if ((vmap.u.mapping.mmu_flags & ZX_VM_PERM_EXECUTE) == 0) {
      continue;
    }
    // Read one byte from each page of this executable mapping.
    // Using volatile ensures the memory access is not discarded: https://godbolt.org/z/YdzEPo
    //
    // Caveat: We assume that mappings are not removed concurrently; if that were to happen,
    // these accesses could crash. Currently, there is one case where we remove mappings: when
    // the tuning manager loads a new effects pipeline. This can dlclose() a previously loaded
    // shared object. Since the tuning manager is not being used at the moment, we don't bother
    // supporting this case.
    auto base = reinterpret_cast<volatile char*>(vmap.base);
    for (auto ptr = base; ptr < base + vmap.size; ptr += kPageSizeBytes) {
      (*ptr);
    }
    total_bytes += vmap.size;
  }

  TRACE_INSTANT("audio", "Pinned bytes", TRACE_SCOPE_THREAD, total_bytes);

  std::lock_guard<std::mutex> lock(mutex_);
  if (total_bytes != last_pinned_bytes_) {
    last_pinned_bytes_ = total_bytes;
    auto end_time = zx::clock::get_monotonic();
    FX_LOGS(INFO) << "pinned " << total_bytes << " executable bytes in "
                  << (end_time - start_time).to_nsecs() << " ns";
  }
}

std::vector<zx_info_maps_t> PinExecutableMemory::ListVMaps() {
  auto proc = zx::process::self();

  // Call first to get the number of mappings.
  size_t actual;
  size_t avail;
  auto status = proc->get_info(ZX_INFO_PROCESS_MAPS, nullptr, 0, &actual, &avail);
  if (status != ZX_OK) {
    FX_PLOGS(WARNING, status) << "Couldn't get process vmaps";
    return std::vector<zx_info_maps_t>();
  }

  // Call again to get the actual mappings. In theory avail can be larger if mappings
  // are being added concurrently. In practice we don't expect that to happen, and in
  // any case, we'll get those new mappings at the next pin, after kTimeBetweenPins.
  std::vector<zx_info_maps_t> out(avail);
  status =
      proc->get_info(ZX_INFO_PROCESS_MAPS, &out[0], out.size() * sizeof(out[0]), &actual, &avail);
  if (status != ZX_OK) {
    FX_PLOGS(WARNING, status) << "Couldn't get process vmaps";
    return std::vector<zx_info_maps_t>();
  }
  out.resize(actual);
  return out;
}

}  // namespace media::audio
