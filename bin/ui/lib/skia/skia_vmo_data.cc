// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/lib/skia/skia_vmo_data.h"

#include <mx/process.h>

#include <atomic>

#include "apps/tracing/lib/trace/event.h"
#include "lib/ftl/logging.h"

static_assert(sizeof(mx_size_t) == sizeof(uint64_t),
              "Fuchsia should always be 64-bit");

namespace mozart {
namespace {

std::atomic<int32_t> g_count;

void TraceCount(int32_t delta) {
  int32_t count = g_count.fetch_add(delta, std::memory_order_relaxed) + delta;
  TRACE_COUNTER1("gfx", "SkDataVmo", 0u, "count", count);
}

void UnmapMemory(const void* buffer, void* context) {
  mx_status_t status =
      mx::process::self().unmap_vm(reinterpret_cast<uintptr_t>(buffer), 0u);
  FTL_CHECK(status == NO_ERROR);
  TraceCount(-1);
}

}  // namespace

sk_sp<SkData> MakeSkDataFromVMO(const mx::vmo& vmo) {
  uint64_t size = 0u;
  mx_status_t status = vmo.get_size(&size);
  if (status != NO_ERROR)
    return nullptr;

  uintptr_t buffer = 0u;
  status =
      mx::process::self().map_vm(vmo, 0u, size, &buffer, MX_VM_FLAG_PERM_READ);
  if (status != NO_ERROR)
    return nullptr;

  sk_sp<SkData> data = SkData::MakeWithProc(reinterpret_cast<void*>(buffer),
                                            size, &UnmapMemory, nullptr);
  if (!data) {
    FTL_LOG(ERROR) << "Could not create SkData";
    status = mx::process::self().unmap_vm(buffer, 0u);
    FTL_CHECK(status == NO_ERROR);
    return nullptr;
  }

  TraceCount(1);
  return data;
}

}  // namespace mozart
