// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <string.h>
#include <zircon/assert.h>

#include <lk/init.h>
#include <vm/vm_aspace.h>

#include "asan-internal.h"

namespace {

void asan_late_init(unsigned int arg) {
  auto status =
      VmAspace::kernel_aspace()->ReserveSpace("kasan-shadow", kAsanShadowSize, KASAN_SHADOW_OFFSET);
  ZX_ASSERT(status == ZX_OK);
}

}  // namespace

LK_INIT_HOOK(asan_late_init, asan_late_init, LK_INIT_LEVEL_VM)
