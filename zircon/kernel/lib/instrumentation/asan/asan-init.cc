// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <string.h>
#include <zircon/assert.h>

#include <lk/init.h>
#include <vm/physmap.h>
#include <vm/pmm.h>
#include <vm/vm_aspace.h>

#include "asan-internal.h"

extern "C" void __asan_register_globals_late();

namespace {

void asan_early_init(unsigned int arg) {
  arch_asan_reallocate_shadow();
  g_asan_initialized.store(true);
}

void asan_late_init(unsigned int arg) {
  auto status =
      VmAspace::kernel_aspace()->ReserveSpace("kasan-shadow", kAsanShadowSize, KASAN_SHADOW_OFFSET);
  ZX_ASSERT(status == ZX_OK);
  pmm_asan_poison_all_free_pages();

  asan_register_globals_late();
}

}  // namespace

LK_INIT_HOOK(asan_early_init, asan_early_init, LK_INIT_LEVEL_VM_PREHEAP)
LK_INIT_HOOK(asan_late_init, asan_late_init, LK_INIT_LEVEL_VM)
