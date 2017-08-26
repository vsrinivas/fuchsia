// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/lib/skia/skia_vmo_data.h"

#include <atomic>

#include <trace/event.h>
#include <mx/vmar.h>

#include "lib/ftl/logging.h"

static_assert(sizeof(size_t) == sizeof(uint64_t),
              "Fuchsia should always be 64-bit");

namespace mozart {
namespace {

std::atomic<int32_t> g_count;

void TraceCount(int32_t delta) {
  int32_t count = g_count.fetch_add(delta, std::memory_order_relaxed) + delta;
  TRACE_COUNTER("gfx", "SkDataVmo", 0u, "count", count);
}

void UnmapMemory(const void* buffer, void* context) {
  const uint64_t size = reinterpret_cast<uint64_t>(context);
  mx_status_t status =
      mx::vmar::root_self().unmap(reinterpret_cast<uintptr_t>(buffer), size);
  FTL_CHECK(status == MX_OK);
  TraceCount(-1);
}

}  // namespace

sk_sp<SkData> MakeSkDataFromVMO(const mx::vmo& vmo) {
  uint64_t size = 0u;
  mx_status_t status = vmo.get_size(&size);
  if (status != MX_OK)
    return nullptr;

  uintptr_t buffer = 0u;
  status =
      mx::vmar::root_self().map(0, vmo, 0u, size, MX_VM_FLAG_PERM_READ, &buffer);
  if (status != MX_OK)
    return nullptr;

  sk_sp<SkData> data = SkData::MakeWithProc(reinterpret_cast<void*>(buffer),
                                            size, &UnmapMemory,
                                            reinterpret_cast<void*>(size));
  if (!data) {
    FTL_LOG(ERROR) << "Could not create SkData";
    status = mx::vmar::root_self().unmap(buffer, size);
    FTL_CHECK(status == MX_OK);
    return nullptr;
  }

  TraceCount(1);
  return data;
}

}  // namespace mozart
