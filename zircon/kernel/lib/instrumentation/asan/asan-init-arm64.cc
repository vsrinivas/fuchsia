// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/counters.h>
#include <lib/instrumentation/asan.h>
#include <stdlib.h>
#include <string.h>
#include <trace.h>
#include <zircon/assert.h>
#include <zircon/types.h>

#include <fbl/ref_ptr.h>
#include <ktl/move.h>
#include <vm/physmap.h>
#include <vm/pmm.h>
#include <vm/vm.h>
#include <vm/vm_address_region.h>
#include <vm/vm_aspace.h>
#include <vm/vm_object_paged.h>

#include "asan-internal.h"

void asan_remap_shadow(uintptr_t start, size_t size) {}

void arch_asan_early_init() {
  // (In the future) the early boot code will map the entire shadow memory to a single zero page.
  // arch_asan_reallocate_shadow will replace the zero page with freshly-allocated pages, allowing
  // kasan to poison and unpoison memory via writes to the shadow.
  // This will be done after the PMM and VM are initialized, to allow instrumenting most
  // allocations in the life of a system.
  // TODO(fxbug.dev/30033): Right now, the boot code doesn't do anything to map shadow memory, so
  // the shadow will only be valid after arch_asan_reallocate_shadow().

  // TODO(fxbug.dev/30033): Not yet.
  // g_asan_initialized.store(true);
}

void arch_asan_late_init() {}
