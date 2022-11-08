// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/shared/pin_executable_memory.h"

#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>
#include <lib/zx/clock.h>
#include <lib/zx/process.h>
#include <lib/zx/thread.h>
#include <lib/zx/vmar.h>
#include <zircon/status.h>

#include <iomanip>

namespace media::audio {

// Memory is considered "unused" if it has not been touched for more than 30s.
// However in critical situations (to avoid OOM), memory not touched in 10s might be evicted.
// To keep all executable memory pinned, we must run at least once every 10s.
// To ensure we never miss a deadline, do this twice every 10s.
static constexpr auto kTimeBetweenPins = zx::sec(5);

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
  size_t marks = 0;

  // We recompute this each Pin() call.
  auto old_pinned_bytes = pinned_bytes_;
  pinned_bytes_ = 0;

  for (auto& vmap : ListVMaps()) {
    // Strategy:
    //
    // We have learned that page faults can be a bottleneck during audio mixing. Our goal
    // is to avoid page faults on the critical path. If we don't allocate memory during the
    // critical path, we can avoid page faults that allocate new pages. However, the kernel
    // may evict previously allocated pages at any time -- even if we avoid allocations, we
    // may still page fault to restore pages that had been previously evicted.
    //
    // The goal of this function is to "pin" memory to avoid evictions. Conceptually, we
    // consider two kinds of evictions:
    //
    //   1. Evictions of pages backed by storage on disk. Since we currently do not use swap
    //      space, this sort of eviction applies only to executable and .rodata pages, which are
    //      backed by the executable image in stable storage.
    //
    //   2. Kernel optimizations that temporarily remove mappings which can be recreated.
    //      This includes optimizations to evict page table pages and optimizations to dedup
    //      pages with the same content (such as dedupping pages that are all zeros).
    //
    // To avoid the first kind of eviction, we mark each read-only mapping ALWAYS NEED.
    // We use "read-only" to encompass both executable pages (which are never writable) and
    // .rodata pages (which are typically mapped read-only). Once a mapping has been marked
    // ALWAYS_NEED, we don't need to mark it again, but we need to periodically update our
    // pins because code may be loaded dynamically with dlopen().
    // See fxrev.dev/583785.
    //
    // To avoid the second kind of eviction, it is currently sufficient to mark at least one
    // page ALWAYS_NEED -- this will disable kernel optimizations for the entire address space.
    // Since we always have at least one executable page, we don't need additional work to satisfy
    // this requirement.
    // See fxbug.dev/85056.
    if ((vmap.type != ZX_INFO_MAPS_TYPE_MAPPING) ||
        (vmap.u.mapping.mmu_flags & ZX_VM_PERM_READ) == 0 ||
        (vmap.u.mapping.mmu_flags & ZX_VM_PERM_WRITE) != 0) {
      continue;
    }

    pinned_bytes_ += vmap.size;

    // Skip if already marked.
    auto it = pinned_mappings_.find(vmap.base);
    if (it != pinned_mappings_.end() && it->second.size == vmap.size &&
        it->second.vmo_koid == vmap.u.mapping.vmo_koid &&
        it->second.vmo_offset == vmap.u.mapping.vmo_offset) {
      continue;
    }

    // Mark.
    auto status =
        zx::vmar::root_self()->op_range(ZX_VMAR_OP_ALWAYS_NEED, vmap.base, vmap.size, nullptr, 0);
    if (status != ZX_OK) {
      FX_LOGS_FIRST_N(WARNING, 20)
          << "zx_vmar_op_range(root, ALWAYS_NEED) failed with vmap.base=" << vmap.base
          << " vmap.size=" << vmap.size;
    }

    marks++;
    pinned_mappings_[vmap.base] = {
        .size = vmap.size,
        .vmo_koid = vmap.u.mapping.vmo_koid,
        .vmo_offset = vmap.u.mapping.vmo_offset,
    };
  }

  if (marks > 0 || old_pinned_bytes != pinned_bytes_) {
    TRACE_INSTANT("audio", "Pinned bytes", TRACE_SCOPE_THREAD, pinned_bytes_);
    auto end_time = zx::clock::get_monotonic();
    FX_LOGS(INFO) << "pinned " << pinned_bytes_ << " total bytes: " << marks
                  << " new VMO mappings, " << old_pinned_bytes << " bytes pinned previously, "
                  << (end_time - start_time).to_nsecs() << " ns to update";
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
