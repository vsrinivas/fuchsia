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

#include "asan-internal.h"

namespace {

void asan_early_init(unsigned int arg) { arch_asan_early_init(); }

void asan_late_init(unsigned int arg) {
  arch_asan_late_init();
  pmm_asan_poison_all_free_pages();

  asan_register_globals_late();
}

}  // namespace

LK_INIT_HOOK(asan_early_init, asan_early_init, LK_INIT_LEVEL_VM_PREHEAP)
LK_INIT_HOOK(asan_late_init, asan_late_init, LK_INIT_LEVEL_VM)
