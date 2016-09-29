// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/lib/view_framework/skia/skia_vmo_data.h"

#include <magenta/syscalls.h>
#include <limits>

namespace mozart {

namespace {
void UnmapMemory(const void* buffer, void* context) {
  mx_process_unmap_vm(mx_process_self(), reinterpret_cast<uintptr_t>(buffer),
                      0);
}
}

sk_sp<SkData> MakeSkDataFromVMO(mx_handle_t vmo) {
  uint64_t size = 0;
  mx_status_t status = mx_vmo_get_size(vmo, &size);
  if (status != NO_ERROR || size > std::numeric_limits<mx_size_t>::max())
    return nullptr;
  uintptr_t buffer = 0;
  status = mx_process_map_vm(mx_process_self(), vmo, 0, size, &buffer,
                             MX_VM_FLAG_PERM_READ);
  if (status != NO_ERROR)
    return nullptr;
  return SkData::MakeWithProc(reinterpret_cast<void*>(buffer), size,
                              UnmapMemory, nullptr);
}

}  // namespace mozart
