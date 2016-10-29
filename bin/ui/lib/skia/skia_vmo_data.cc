// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/lib/skia/skia_vmo_data.h"

#include <mx/process.h>

#include "lib/ftl/logging.h"

static_assert(sizeof(mx_size_t) == sizeof(uint64_t),
              "Fuchsia should always be 64-bit");

namespace mozart {
namespace {

void UnmapMemory(const void* buffer, void* context) {
  mx_status_t status =
      mx::process::self().unmap_vm(reinterpret_cast<uintptr_t>(buffer), 0u);
  FTL_CHECK(status == NO_ERROR);
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

  return SkData::MakeWithProc(reinterpret_cast<void*>(buffer), size,
                              &UnmapMemory, nullptr);
}

}  // namespace mozart
