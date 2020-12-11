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

// Disable address sanitizer. While Pin() should never read from unmapped memory
// (i.e., it should never segfault), it might read an address that is not tracked
// by the address sanitizer.
__attribute__((no_sanitize_address)) void PinExecutableMemory::Pin() {
  TRACE_DURATION("audio", "PinExecutableMemory::Pin");

  {
    // Reset so we only accumulate mappings that are discarded concurrently with
    // this current Pin().
    std::lock_guard<std::mutex> lock(mutex_);
    discarded_mappings_.clear();
  }

  auto start_time = zx::clock::get_monotonic();
  size_t total_bytes = 0;
  size_t total_executable_bytes = 0;

  for (auto& vmap : ListVMaps()) {
    // All readable, non-writable pages are eligible for pinning.
    if ((vmap.type != ZX_INFO_MAPS_TYPE_MAPPING) ||
        (vmap.u.mapping.mmu_flags & ZX_VM_PERM_READ) == 0 ||
        (vmap.u.mapping.mmu_flags & ZX_VM_PERM_WRITE) != 0) {
      continue;
    }

    // We want to pin this RO mapping. We assume that executable mappings are not removed
    // concurrently. If that were to happen, these accesses could crash. Currently, there is one
    // case where we remove executable mappings: when the tuning manager loads a new effects
    // pipeline. This can dlclose() a previously loaded shared object. Since the tuning manager
    // is not being used at the moment, we don't bother supporting this case.
    //
    // Non-executable mappings might be removed concurrently with this method, between the
    // above ListVMaps() call and here. For example, renderer payload buffers might use read-only
    // shared VMOs and those mappings can come and go as renderers are created and destroyed.
    // To handle this race, we use the below lock to make pinning atomic with VMO destruction.
    // To minimize lock contention, we lock each mapping rather than locking the entire Pin().
    std::unique_lock<std::mutex> lock(mutex_, std::defer_lock);

    const bool executable = ((vmap.u.mapping.mmu_flags & ZX_VM_PERM_EXECUTE) != 0);
    if (!executable) {
      lock.lock();
    }

    // Read one byte from each page of this executable mapping.
    // Using volatile ensures the memory access is not discarded: https://godbolt.org/z/YdzEPo
    //
    auto base = reinterpret_cast<volatile char*>(vmap.base);
    for (auto ptr = base; ptr < base + vmap.size; ptr += kPageSizeBytes) {
      if (!executable) {
        AssertMutexHeld();
        if (ShouldSkip(reinterpret_cast<size_t>(ptr))) {
          continue;
        }
      }
      (*ptr);
    }
    total_bytes += vmap.size;
    if (executable) {
      total_executable_bytes += vmap.size;
    }
  }

  TRACE_INSTANT("audio", "Pinned bytes", TRACE_SCOPE_THREAD, total_bytes);

  auto end_time = zx::clock::get_monotonic();
  FX_LOGS(INFO) << "pinned " << total_bytes << " bytes (" << total_executable_bytes
                << " executable bytes) in " << (end_time - start_time).to_nsecs() << " ns";
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

bool PinExecutableMemory::ShouldSkip(size_t addr) const {
  // Assuming this is usually empty, or at most has just a few mappings, hence O(n) is ok.
  for (auto& m : discarded_mappings_) {
    if (m.start <= addr && addr < m.end) {
      return true;
    }
  }
  return false;
}

}  // namespace media::audio
